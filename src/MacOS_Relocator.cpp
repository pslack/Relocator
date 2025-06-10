// src/MacOS_Relocator.cpp
#include "MacOS_Relocator.hpp"
#include "Process.hpp"
#include <iostream>
#include <sstream>
#include <set>
#include <map>
#include <regex> // For regex matching
#include <dlfcn.h>
#include <cstdlib>
#include <algorithm>

// Helper function to trim strings
static std::string trim(const std::string& s) {
    size_t first = s.find_first_not_of(" \t\n\r");
    if (std::string::npos == first) {
        return s;
    }
    size_t last = s.find_last_not_of(" \t\n\r");
    return s.substr(first, (last - first + 1));
}

void MacOS_Relocator::bundleDependencies(
    const std::filesystem::path& inputFile,
    const std::filesystem::path& outputDir,
    const std::vector<std::string>& searchPaths,
    const std::vector<std::string>& literalExclusions,
    const std::vector<std::string>& regexExclusions) {

    std::cout << "\n=== Starting macOS Relocation ===\n";

    std::set<std::filesystem::path> filesToBundle;
    std::vector<std::filesystem::path> filesToProcess = { std::filesystem::canonical(inputFile) };

    // Create a set from the literal exclusions for fast lookups
    const std::set<std::string> exclusionSet(literalExclusions.begin(), literalExclusions.end());

    std::cout << "Phase 1: Discovering all dependencies..." << std::endl;
    while (!filesToProcess.empty()) {
        std::filesystem::path currentFile = filesToProcess.back();
        filesToProcess.pop_back();

        if (filesToBundle.count(currentFile)) {
            continue;
        }

        std::cout << "  Processing: " << currentFile << std::endl;
        filesToBundle.insert(currentFile);

        std::string otoolCmd = "otool -L \"" + currentFile.string() + "\"";
        CommandResult result = Process::exec(otoolCmd);
        if (result.exitStatus != 0) {
            throw std::runtime_error("otool failed for " + currentFile.string());
        }

        std::istringstream stream(result.output);
        std::string line;
        std::getline(stream, line);

        while (std::getline(stream, line)) {
            std::string depPathStr = trim(line);
            depPathStr = depPathStr.substr(0, depPathStr.find(" ("));

            if (depPathStr.rfind("/usr/lib/", 0) == 0 || depPathStr.rfind("/System/Library/", 0) == 0) {
                continue;
            }

            std::filesystem::path depPath(depPathStr);
            std::string filename = depPath.filename().string();

            // Check literal exclusion list
            if (exclusionSet.count(filename)) {
                std::cout << "    --> Ignoring user-excluded library: " << filename << std::endl;
                continue;
            }

            // NEW: Check regex exclusion list
            bool excludedByRegex = false;
            for (const auto& pattern : regexExclusions) {
                if (std::regex_match(filename, std::regex(pattern))) {
                    excludedByRegex = true;
                    std::cout << "    --> Ignoring user-excluded regex pattern '" << pattern << "': " << filename << std::endl;
                    break;
                }
            }
            if (excludedByRegex) {
                continue;
            }

            if (depPathStr.rfind("@rpath", 0) == 0) {
                 std::cerr << "  Warning: @rpath dependency found. Resolution not yet implemented: " << depPathStr << std::endl;
                 continue;
            }

            if (std::filesystem::exists(depPath)) {
                filesToProcess.push_back(std::filesystem::canonical(depPath));
            } else {
                std::cerr << "  Warning: Could not find dependency: " << depPathStr << std::endl;
            }
        }
    }
    std::cout << "\n--- Dependency Analysis Complete ---" << std::endl;
    std::cout << "Found " << filesToBundle.size() << " total files to process.\n\n";

    // --- Phase 2: Copy all discovered files to the output directory ---
    std::cout << "Phase 2: " << (m_dryRun ? "Planning to copy" : "Copying") << " dependencies to output directory..." << std::endl;
    std::map<std::filesystem::path, std::filesystem::path> oldToNewPathMap;

    for (const auto& originalPath : filesToBundle) {
        std::filesystem::path destination = outputDir / originalPath.filename();
        oldToNewPathMap[originalPath] = destination;

        bool shouldCopy = true;
        // In a dry run, we don't check for equivalency since the destination doesn't exist.
        if (!m_dryRun && std::filesystem::exists(destination)) {
            try {
                if (std::filesystem::canonical(originalPath) == std::filesystem::canonical(destination)) {
                    std::cout << "  Skipping copy for file already in destination: " << originalPath.filename() << std::endl;
                    shouldCopy = false;
                }
            } catch (const std::filesystem::filesystem_error& e) {
                std::cout << "  Could not check equivalency for " << destination << ", will overwrite." << std::endl;
            }
        }

        if (shouldCopy) {
            std::cout << "  " << (m_dryRun ? "Would copy" : "Copying") << " " << originalPath.filename() << " to " << destination << std::endl;
            if (!m_dryRun) {
                std::filesystem::copy_file(originalPath, destination, std::filesystem::copy_options::overwrite_existing);
            }
        }
    }

    // --- Phase 3: Fixup the copied libraries ---
    std::cout << "\nPhase 3: " << (m_dryRun ? "Planning to relocate" : "Relocating") << " library paths for all bundled files..." << std::endl;
    for (const auto& [originalPath, newPath] : oldToNewPathMap) {

        std::cout << "  Fixing up " << newPath.filename() << "..." << std::endl;

        // Step 3a: Change the library's own ID to be relocatable using @loader_path
        std::string newId = "@loader_path/" + newPath.filename().string();
        std::string idCmd = "install_name_tool -id \"" + newId + "\" \"" + newPath.string() + "\"";
        std::cout << "    " << idCmd << std::endl;
        if (!m_dryRun) {
            std::filesystem::permissions(newPath, std::filesystem::perms::owner_write, std::filesystem::perm_options::add);
            Process::exec(idCmd);
        }

        // Step 3b: Find its original dependencies and change their paths to point to our new bundled copies.
        std::string otoolCmd = "otool -L \"" + originalPath.string() + "\"";
        CommandResult result = Process::exec(otoolCmd);
        std::istringstream stream(result.output);
        std::string line;
        std::getline(stream, line); // skip self

        while (std::getline(stream, line)) {
            std::string oldDepPathStr = trim(line);
            oldDepPathStr = oldDepPathStr.substr(0, oldDepPathStr.find(" ("));

            std::filesystem::path oldDepPath(oldDepPathStr);
            if (std::filesystem::exists(oldDepPath)) {
                auto canonicalDepPath = std::filesystem::canonical(oldDepPath);

                // Check if this dependency is one we have bundled
                if (oldToNewPathMap.count(canonicalDepPath)) {
                    std::filesystem::path newDepPath = oldToNewPathMap.at(canonicalDepPath);
                    // FIX: Correctly use the 'newDepPath' variable here instead of the non-existent 'newDep'
                    std::string newDepRpath = "@loader_path/" + newDepPath.filename().string();

                    // IMPORTANT: Run 'change' on the *new* file, not the original.
                    std::string changeCmd = "install_name_tool -change \"" + oldDepPathStr + "\" \"" + newDepRpath + "\" \"" + newPath.string() + "\"";
                    std::cout << "    " << changeCmd << std::endl;
                    if (!m_dryRun) {
                        Process::exec(changeCmd);
                    }
                }
            }
        }
    }

    // --- Phase 4: Verify the relocated library ---
    std::cout << "\nPhase 4: " << (m_dryRun ? "Planning to verify" : "Verifying") << " bundled library..." << std::endl;
    if (!m_dryRun) {
        auto canonicalInputPath = std::filesystem::canonical(inputFile);
        if (oldToNewPathMap.count(canonicalInputPath)) {
            std::filesystem::path primaryLibInBundle = oldToNewPathMap.at(canonicalInputPath);
            std::cout << "  Attempting to dynamically load: " << primaryLibInBundle << std::endl;

            setenv("DYLD_PRINT_LIBRARIES", "1", 1);

            void* handle = dlopen(primaryLibInBundle.c_str(), RTLD_LAZY);
            if (!handle) {
                std::cerr << "\n  VERIFICATION FAILED: dlopen reported an error:\n  " << dlerror() << std::endl;
            } else {
                std::cout << "\n  Verification SUCCESS: Library loaded successfully." << std::endl;
                dlclose(handle);
            }

            unsetenv("DYLD_PRINT_LIBRARIES");
        } else {
            std::cerr << "\n  VERIFICATION FAILED: Could not find primary library in the bundle map." << std::endl;
        }
    } else {
        std::cout << "  Would set DYLD_PRINT_LIBRARIES=1 and test loading the main library with dlopen." << std::endl;
    }
}
