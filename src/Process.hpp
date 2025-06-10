#ifndef RELOCATOR_PROCESS_HPP
#define RELOCATOR_PROCESS_HPP

#include <string>
#include <vector>
#include <iostream>
#include <array>
#include <stdexcept>

/**
 * @struct CommandResult
 * @brief Holds the output and exit status of an executed shell command.
 */
struct CommandResult {
    std::string output;
    int exitStatus;
};

/**
 * @class Process
 * @brief A utility class for executing external shell commands.
 */
class Process {
public:
    /**
     * Executes a system command and captures its standard output.
     * @param command The command string to execute.
     * @return A CommandResult struct containing the output and exit status.
     * @throws std::runtime_error if popen() fails.
     */
    static CommandResult exec(const std::string& command) {
        int exitcode = 0;
        std::string result;
        std::array<char, 128> buffer{};

#ifdef _WIN32
        #define popen _popen
#define pclose _pclose
#endif

        FILE* pipe = popen(command.c_str(), "r");
        if (pipe == nullptr) {
            throw std::runtime_error("popen() failed for command: " + command);
        }

        try {
            while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
                result += buffer.data();
            }
        } catch (...) {
            pclose(pipe);
            throw;
        }

        exitcode = pclose(pipe);

        // Trim trailing newline if it exists
        if (!result.empty() && result[result.length() - 1] == '\n') {
            result.erase(result.length() - 1);
        }

        return CommandResult{result, exitcode};
    }
};

#endif //RELOCATOR_PROCESS_HPP
