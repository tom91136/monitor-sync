#include "args.hxx"
#include "asio.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <thread>

extern "C" {
#include <X11/Xlib.h>
#include <X11/extensions/dpms.h>
}

struct MonitorState {
  int64_t epochMs;
  double statePollRateHz;
  bool poweredOn;
};

std::optional<bool> dpmsGetMonitorPower(Display *dpy) {
  int dummy;
  if (DPMSQueryExtension(dpy, &dummy, &dummy) && DPMSCapable(dpy)) {
    CARD16 state;
    BOOL enabled;
    DPMSInfo(dpy, &state, &enabled);
    return !(enabled && state == DPMSModeOff);
  } else
    return {};
}

void resetDPMS(Display *dpy) {
  XSetScreenSaver(dpy, 0, 0, 1, 1);
  DPMSSetTimeouts(dpy, 0, 0, 0); // so that our DPMS state don't change based on time
  XFlush(dpy);
}

bool dpmsSetMonitorPower(Display *dpy, bool on) {
  int dummy;
  if (DPMSQueryExtension(dpy, &dummy, &dummy) && DPMSCapable(dpy)) {
    DPMSEnable(dpy);
    // see https://github.com/freedesktop/xorg-xset/blob/41b3ad04db4f9fdcf2705445a28c9cceecf6d980/xset.c#L584
    std::this_thread::sleep_for(std::chrono::microseconds(100000));
    resetDPMS(dpy);
    DPMSForceLevel(dpy, on ? DPMSModeOn : DPMSModeOff);
    return true;
  } else
    return false;
}

int64_t epochMsNow() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::condition_variable cv;
std::atomic_bool interrupted;

void startClient(const std::optional<std::string> &multicastAddr, asio::ip::port_type port,
                 const std::function<void(MonitorState)> &f) {
  asio::io_context context;
  asio::ip::udp::endpoint endpoint(asio::ip::udp::v4(), port);
  asio::ip::udp::socket socket(context);
  socket.open(endpoint.protocol());
  socket.set_option(asio::ip::udp::socket::reuse_address(true));
  socket.bind(endpoint);

  if (multicastAddr) {
    socket.set_option(asio::ip::multicast::join_group(asio::ip::make_address(*multicastAddr).to_v4()));
  }

  MonitorState state{};
  asio::ip::udp::endpoint unused;
  std::function<void()> waitForMessage = [&]() {
    socket.async_receive_from(asio::buffer(&state, sizeof(MonitorState)), unused, [&](std::error_code, size_t) {
      f(state);
      waitForMessage();
    });
  };
  waitForMessage();
  while (!interrupted) {
    context.poll_one();
  }
}

void startServer(const std::optional<std::string> &multicastAddr, asio::ip::port_type port, double rateHz,
                 const std::function<std::optional<MonitorState>()> &f) {
  asio::io_context context;
  asio::ip::udp::socket socket(context);
  socket.open(asio::ip::udp::v4());
  socket.set_option(asio::socket_base::broadcast(true));
  socket.set_option(asio::ip::udp::socket::reuse_address(true));
  socket.set_option(asio::ip::multicast::enable_loopback(true));

  asio::ip::udp::endpoint destination(
      multicastAddr ? asio::ip::make_address(*multicastAddr) : asio::ip::address_v4::broadcast(), port);
  std::chrono::milliseconds duration(int(std::round(1000.0 / rateHz)));

  std::mutex m;
  while (!interrupted) {
    if (auto state = f()) {
      socket.send_to(asio::buffer(&state, sizeof(MonitorState)), destination);
    }
    std::unique_lock<std::mutex> lock(m);
    if (cv.wait_for(lock, duration, [] { return interrupted.load(); })) {
      break;
    }
  }
}

int main(int argc, char *argv[]) {

  for (auto sig : {SIGINT, SIGTERM})
    std::signal(sig, [](int) {
      interrupted = true;
      cv.notify_all();
    });

  args::ArgumentParser parser("monitor-sync", "Client/server for syncing DPMS (monitor power) states");
  parser.helpParams.showCommandChildren = true;
  parser.helpParams.addDefault = true;
  parser.helpParams.width = 180;

  args::CompletionFlag completion(parser, {"complete"});
  args::Group arguments(parser, "arguments", args::Group::Validators::DontCare, args::Options::Global);
  args::HelpFlag h(arguments, "help", "help", {'h', "help"});
  args::Group commands(arguments, "commands");
  args::Command server(commands, "server", //
                       "Start the server, use this on the machine you are monitoring DPMS of");
  args::ValueFlag<double> rateHz(server, "RATE", //
                                 "The rate in hertz at which X11's DPMS state is polled", {'r', "rate"}, 1.0);
  args::Command client(commands, "client", //
                       "Start the client, use this on machines that needs to have the same DPMS state as the server");
  args::ValueFlag<std::string> multicastAddress(client, "IP", //
                                                "The *multicast* (i.e starts with 224.*.*.*, this is NOT the your "
                                                "normal IP address) address, defaults to broadcast if not specified",
                                                {'i', "ip"});
  args::ValueFlag<std::string> display(arguments, "DISPLAY", //
                                       "The X display to use (e.g `:0`), omit for the default display",
                                       {'d', "display"});
  args::ValueFlag<int> port(arguments, "PORT", //
                            "The UDP port to use for sending/receiving sync messages", {'p', "port"}, 3000);

  try {
    parser.ParseCLI(argc, argv);
  } catch (const args::Completion &e) {
    std::cout << e.what();
    return EXIT_SUCCESS;
  } catch (args::Help &) {
    std::cout << parser;
    return EXIT_SUCCESS;
  } catch (args::Error &e) {
    std::cerr << e.what() << std::endl << parser;
    return EXIT_FAILURE;
  }
  auto fmtBool = [](bool v) { return v ? "ON" : "OFF"; };

  auto address = multicastAddress ? std::make_optional(multicastAddress.Get()) : std::nullopt;

  auto run = [&](Display *dpy, const std::string &displayName) {
    if (server) {
      std::cout << "Polling DPMS state (" << rateHz.Get() << "Hz) on display " << displayName << " and broadcasting on "
                << address.value_or("(any)") << ":" << port.Get() << std::endl;

      std::optional<bool> lastState = {};
      startServer(address, port.Get(), rateHz.Get(), [&]() -> std::optional<MonitorState> {
        auto currentState = dpmsGetMonitorPower(dpy).value_or(false);
        using namespace std::chrono;

        if (!lastState || currentState != *lastState) {
          std::cout << "sync: " << (lastState ? fmtBool(*lastState) : "NONE") << " -> " << fmtBool(currentState)
                    << std::endl;
          lastState = currentState;
          return MonitorState{epochMsNow(), rateHz.Get(), currentState};
        } else {
          return {};
        }
      });
      std::cout << "Stopping..." << std::endl;

    } else if (client) {
      std::cout << "Listening for DPMS state from " << address.value_or("(any)") << ":" << port.Get()
                << " using display " << displayName << std::endl;

      resetDPMS(dpy);

      startClient(address, port.Get(), [&](const MonitorState &p) {
        auto now = epochMsNow();

        auto toleranceMs = 1000.0 / p.statePollRateHz;
        auto deltaMs = double(std::abs(now - p.epochMs));
        if (deltaMs > toleranceMs) {
          std::cerr << "Received message timestamp tolerance: " << deltaMs << " > " << toleranceMs
                    << " (rate=" << p.statePollRateHz << "Hz ), ignoring...";
        } else {
          if (auto current = dpmsGetMonitorPower(dpy)) {
            if (*current != p.poweredOn) {
              std::cout << "sync: " << fmtBool(current.value()) << " -> " << fmtBool(p.poweredOn) << std::endl;

              size_t i = 0;
              while (i <= 100) {
                if (!dpmsSetMonitorPower(dpy, p.poweredOn)) std::cerr << "warn: DPMS set failed";
                std::this_thread::sleep_for(std::chrono::microseconds(100000));
                if (dpmsGetMonitorPower(dpy).value_or(false) == p.poweredOn) break;
                i++;
              }

              if (!dpmsSetMonitorPower(dpy, p.poweredOn)) {
                std::cerr << "warn: DPMS set failed";
              }
            }
          } else {
            std::cerr << "warn: DPMS not enabled";
          }
        }
      });
      std::cout << "Stopping..." << std::endl;

    } else {
      std::cout << "Command not specified.\b" << parser << std::endl;
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  };

  int code = EXIT_SUCCESS;
  if (auto dpy = XOpenDisplay(display ? display.Get().data() : nullptr)) {
    const auto displayName = std::string(DisplayString(dpy));
    if (auto init = dpmsGetMonitorPower(dpy)) {
      std::cout << "DPMS available, power=" << fmtBool(*init) << std::endl;
      code = run(dpy, displayName);
    } else {
      std::cerr << "DPMS not supported" << std::endl;
      code = EXIT_FAILURE;
    }
    if (XCloseDisplay(dpy) == Success) std::cout << "Display " << displayName << " closed" << std::endl;
    else
      std::cerr << "warn: display " << displayName << " failed to close" << std::endl;
  } else {
    std::cerr << "Cannot connect to XDisplay" << std::endl;
    code = EXIT_FAILURE;
  }
  return code;
}
