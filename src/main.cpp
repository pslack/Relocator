#include <iostream>
#include <memory> // For std::unique_ptr
#include <stdexcept>

#include "CLI/CLI.hpp"
#include "Relocator.hpp"

// Forward declare the factory function to create the platform-specific relocator
std::unique_ptr<Relocator> createPlatformRelocator(bool dryRun);

int main(int argc, char* argv[]) {
    // --- Command-Line Argument Parsing ---
    CLI::App app{"A tool to bundle shared library dependencies for an executable or library."};
    app.set_version_flag("--version", "1.0.0");

    std::filesystem::path inputFile;
    app.add_option("-i,--input", inputFile, "The main library or executable to fix up")
            ->required()
            ->check(CLI::ExistingFile);

    std::filesystem::path outputDir;
    app.add_option("-o,--output", outputDir, "The directory to copy dependencies into (e.g., YourApp.app/Contents/Frameworks)")
            ->required();

    std::vector<std::string> searchPaths;
    app.add_option("-s,--search", searchPaths, "Additional directories to search for libraries");

    bool dryRun = false;
    app.add_flag("-d,--dry-run", dryRun, "Print commands without executing them");

    CLI11_PARSE(app, argc, argv);

    // --- Main Logic ---
    try {
        // Use a factory to get the correct relocator for the current OS
        std::unique_ptr<Relocator> relocator = createPlatformRelocator(dryRun);

        // Run the relocation process
        relocator->bundleDependencies(inputFile, outputDir, searchPaths);

    } catch (const std::exception& e) {
        std::cerr << "An error occurred: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "Relocation complete." << std::endl;

    return 0;
}

// --- Platform-Specific Factory Implementation ---
// This function is the entry point to the platform-specific code.
// CMake will compile the correct .cpp file based on the OS.

#if defined(__APPLE__)
#include "MacOS_Relocator.hpp"
std::unique_ptr<Relocator> createPlatformRelocator(bool dryRun) {
    return std::make_unique<MacOS_Relocator>(dryRun);
}
#elif defined(__linux__)
#include "Linux_Relocator.hpp"
std::unique_ptr<Relocator> createPlatformRelocator(bool dryRun) {
    return std::make_unique<Linux_Relocator>(dryRun);
}
#elif defined(_WIN32)
#include "Windows_Relocator.hpp"
std::unique_ptr<Relocator> createPlatformRelocator(bool dryRun) {
    return std::make_unique<Windows_Relocator>(dryRun);
}
#else
std::unique_ptr<Relocator> createPlatformRelocator(bool dryRun) {
    throw std::runtime_error("Unsupported platform.");
}
#endif
