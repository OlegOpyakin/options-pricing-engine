#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "ope/ch_config.hpp"
#include "ope/inserter.hpp"

namespace {

void print_help() {
  std::cout <<
      "Commands:\n"
      "  insert <path>   Import a JSON file or a directory (recursively) into ClickHouse\n"
      "  help            Show this help\n"
      "  quit | exit     Leave the program\n";
}

std::vector<std::string> tokenize(const std::string& line) {
  std::vector<std::string> tokens;
  std::istringstream iss(line);
  std::string tok;
  while (iss >> tok) tokens.push_back(tok);
  return tokens;
}

// Dispatch a single command. Returns false when the loop should stop.
bool dispatch(const std::vector<std::string>& args, ope::Inserter& inserter) {
  if (args.empty()) return true;
  const std::string& cmd = args[0];

  if (cmd == "quit" || cmd == "exit") return false;
  if (cmd == "help") {
    print_help();
    return true;
  }
  if (cmd == "insert") {
    if (args.size() < 2) {
      std::cerr << "usage: insert <path>\n";
      return true;
    }
    const bool ok = inserter.insert(args[1]);
    std::cout << (ok ? "insert: OK" : "insert: FAILED") << "\n";
    return true;
  }

  std::cerr << "unknown command: " << cmd << " (type 'help')\n";
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  const ope::ChConfig config = ope::ChConfig::from_env();
  ope::Inserter inserter(config);

  // Non-interactive mode: treat argv as a single command, run it, and exit.
  // e.g.  ./ope insert data/CME
  if (argc > 1) {
    std::vector<std::string> args(argv + 1, argv + argc);
    const bool cont = dispatch(args, inserter);
    (void)cont;
    return 0;
  }

  // Interactive event loop. `insert` is one of several commands; more data
  // interaction commands can be added to dispatch() later.
  std::cout << "Options Pricing Engine — data console\n";
  print_help();

  std::string line;
  while (true) {
    std::cout << "> " << std::flush;
    if (!std::getline(std::cin, line)) break;  // EOF (Ctrl-D)
    if (!dispatch(tokenize(line), inserter)) break;
  }
  return 0;
}
