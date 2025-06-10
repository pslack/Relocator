// src/Linux_Relocator.cpp
#include "Linux_Relocator.hpp"
#include "Process.hpp"
#include <iostream>
#include <sstream>
#include <set>
#include <map>

// Helper function to trim strings
static std::string trim(const std::string& s) {
    size_t first = s.find_first_not_of(" \t\n\r");
    if (std::string::npos == first) {
        return s;
    }
    size_t last = s.find_last_not_of(" \t\n\r");
    return s.substr(first, (last - first + 1));
}

void Linux_Relocator::bundleDependencies(
        const std::filesystem::path& inputFile,
        const std::filesystem::path& outputDir,
        const std::vector<std::string>& searchPaths) {

    std::cout << "\n=== Starting Linux Relocation ===\n";

    std::set<std::filesystem::path> filesToBundle;
    std::vector<std::filesystem::path> filesToProcess = { std::filesystem::canonical(inputFile) };

    std::cout << "Phase 1: Discovering all dependencies..." << std::endl;
    while (!filesToProcess.empty()) {
        std::filesystem::path currentFile = filesToProcess.back();
        filesToProcess.pop_back();

        if (filesToBundle.count(currentFile)) {
            continue;
        }

        std::cout << "  Processing: " << currentFile << std::endl;
        filesToBundle.insert(currentFile);

        // Get dependencies of the current file with ldd
        std::string lddCmd = "ldd \"" + currentFile.string() + "\"";
        CommandResult result = Process::exec(lddCmd);
        if (result.exitStatus != 0) {
            // ldd returns non-zero for non-dynamic executables, which is not an error for us.
            std::cout << "  Info: 'ldd' returned non-zero (likely a static library or non-executable), skipping: " << currentFile.filename() << std::endl;
            continue;
        }

        std::istringstream stream(result.output);
        std::string line;
        while (std::getline(stream, line)) {
            std::stringstream ss(line);
            std::string name, arrow, path, addr;
            ss >> name >> arrow >> path >> addr;

            if (arrow != "=>" || path.empty()) {
                continue;
            }

            // Ignore system libraries
            if (path.rfind("/lib/", 0) == 0 || path.rfind("/usr/lib/", 0) == 0 || path.rfind("/lib64/", 0) == 0) {
                continue;
            }

            std::filesystem::path depPath(path);
            if (std::filesystem::exists(depPath)) {
                filesToProcess.push_back(std::filesystem::canonical(depPath));
            } else {
                std::cerr << "  Warning: Could not find dependency: " << depPath << std::endl;
            }
        }
    }

    std::cout << "\n--- Dependency Analysis Complete ---" << std::endl;
    std::cout << "Found " << filesToBundle.size() << " total files to process.\n\n";

    // --- Phase 2: Copy all discovered files to the output directory ---
    std::cout << "Phase 2: " << (m_dryRun ? "Planning to copy" : "Copying") << " dependencies to output directory..." << std::endl;

    for (const auto& originalPath : filesToBundle) {
        std::filesystem::path destination = outputDir / originalPath.filename();

        bool shouldCopy = true;
        if (!m_dryRun && std::filesystem::exists(destination)) {
            if (std::filesystem::canonical(originalPath) == std::filesystem::canonical(destination)) {
                std::cout << "  Skipping copy for file already in destination: " << originalPath.filename() << std::endl;
                shouldCopy = false;
            }
        }

        if (shouldCopy) {
            std::cout << "  " << (m_dryRun ? "Would copy" : "Copying") << " " << originalPath.filename() << " to " << destination << std::endl;
            if (!m_dryRun) {
                std::filesystem::copy_file(originalPath, destination, std::filesystem::copy_options::overwrite_existing);
            }
        }
    }

    // --- Phase 3: Fixup the copied libraries with patchelf ---
    std::cout << "\nPhase 3: " << (m_dryRun ? "Planning to relocate" : "Relocating") << " library paths for all bundled files..." << std::endl;
    for (const auto& originalPath : filesToBundle) {
        std::filesystem::path newPath = outputDir / originalPath.filename();
        std::cout << "  Fixing up " << newPath.filename() << "..." << std::endl;

        // Use patchelf to set the RPATH to '$ORIGIN'. This tells the loader
        // to look for dependencies in the same directory as the library itself.
        std::string patchelfCmd = "patchelf --set-rpath '$ORIGIN' \"" + newPath.string() + "\"";

        std::cout << "    " << patchelfCmd << std::endl;
        if (!m_dryRun) {
            Process::exec(patchelfCmd);
        }
    }
}