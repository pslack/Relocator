// src/Linux_Relocator.cpp
#include "Linux_Relocator.hpp"
#include "Process.hpp"
#include <iostream>
#include <sstream>
#include <set>
#include <map>
#include <regex>
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

void Linux_Relocator::bundleDependencies(
    const std::filesystem::path& inputFile,
    const std::filesystem::path& outputDir,
    const std::vector<std::string>& searchPaths,
    const std::vector<std::string>& literalExclusions,
    const std::vector<std::string>& regexExclusions) {

    std::cout << "\n=== Starting Linux Relocation ===\n";

    std::map<std::filesystem::path, std::map<std::string, std::filesystem::path>> dependencyGraph;
    std::vector<std::filesystem::path> filesToProcess = { std::filesystem::canonical(inputFile) };

    const std::set<std::string> system_libs_to_ignore = {
        // Core C/C++ runtime and linker
        "linux-vdso.so.1", "ld-linux-x86-64.so.2", "libc.so.6", "libdl.so.2",
        "libm.so.6", "libpthread.so.0", "librt.so.1", "libgcc_s.so.1", "libstdc++.so.6",
        "libresolv.so.2", "libcrypt.so.1"
    };

    // Combine the user's literal exclusions into the main set for fast lookup
    std::set<std::string> allExclusions = system_libs_to_ignore;
    allExclusions.insert(literalExclusions.begin(), literalExclusions.end());

    const std::regex ldd_regex(R"(^\s*([^ \t]+)\s*=>\s*(.+)\s*\(|(.+)\s*\(.+\))");

    std::cout << "Phase 1: Discovering all dependencies..." << std::endl;
    while (!filesToProcess.empty()) {
        std::filesystem::path currentFile = filesToProcess.back();
        filesToProcess.pop_back();

        if (dependencyGraph.count(currentFile)) {
            continue;
        }

        std::cout << "  Processing: " << currentFile << std::endl;
        dependencyGraph[currentFile] = {};

        std::string lddCmd = "ldd \"" + currentFile.string() + "\"";
        CommandResult result = Process::exec(lddCmd);

        std::istringstream stream(result.output);
        std::string line;

        while (std::getline(stream, line)) {
            line = trim(line);
            std::smatch match;
            if (std::regex_search(line, match, ldd_regex)) {
                std::string soname = trim(match[1].str());
                std::string depPathStr = trim(match[2].str());

                if (depPathStr.empty()) { depPathStr = soname; }
                if (depPathStr.empty() || depPathStr == "not a dynamic executable") { continue; }

                std::filesystem::path depPath(depPathStr);
                std::string filename = depPath.filename().string();

                // Check against combined exclusion list
                bool shouldIgnore = false;
                if(allExclusions.count(filename)) {
                    shouldIgnore = true;
                } else {
                    // NEW: Check against user-provided regex patterns
                    for (const auto& pattern : regexExclusions) {
                        if (std::regex_match(filename, std::regex(pattern))) {
                            shouldIgnore = true;
                            break;
                        }
                    }
                }

                if(shouldIgnore) {
                    std::cout << "    --> Ignoring library: " << filename << std::endl;
                    continue;
                }

                if (std::filesystem::exists(depPath)) {
                    auto canonicalDepPath = std::filesystem::canonical(depPath);
                    dependencyGraph[currentFile][soname] = canonicalDepPath;
                    filesToProcess.push_back(canonicalDepPath);
                } else {
                    std::cerr << "  Warning: Could not find dependency: " << depPathStr << std::endl;
                }
            }
        }
    }

    std::cout << "\n--- Dependency Analysis Complete ---" << std::endl;
    std::cout << "Found " << dependencyGraph.size() << " total files to process.\n\n";

    // --- Phase 2: Copy all discovered files to the output directory ---
    std::cout << "Phase 2: " << (m_dryRun ? "Planning to copy" : "Copying") << " dependencies to output directory..." << std::endl;
    for (const auto& [originalPath, dependencies] : dependencyGraph) {
        std::filesystem::path newPath = outputDir / originalPath.filename();

        std::cout << "  " << (m_dryRun ? "Would copy" : "Copying") << " " << originalPath.filename() << " to " << newPath << std::endl;
        if (!m_dryRun) {
            std::filesystem::copy_file(originalPath, newPath, std::filesystem::copy_options::overwrite_existing);
        }
    }

    // --- Phase 3: Fixup the copied libraries with 'patchelf' ---
    std::cout << "\nPhase 3: " << (m_dryRun ? "Planning to relocate" : "Relocating") << " library paths for all bundled files..." << std::endl;
    for (const auto& [originalPath, dependencies] : dependencyGraph) {
        std::filesystem::path newPath = outputDir / originalPath.filename();
        std::cout << "  Fixing up " << newPath.filename() << "..." << std::endl;

        // Step 3a: Set the RPATH to '$ORIGIN' so it looks in its own directory.
        std::string rpathCmd = "patchelf --set-rpath '$ORIGIN' \"" + newPath.string() + "\"";

        std::cout << "    " << rpathCmd << std::endl;
        if (!m_dryRun) {
            Process::exec(rpathCmd);
        }

        // Step 3b: For each of its dependencies that we also bundled,
        // replace the absolute path with just the filename.
        for (const auto& [soname, depOriginalPath] : dependencies) {
            std::string newDepName = depOriginalPath.filename().string();

            // This is the crucial command that rewrites the dependency entries.
            std::string replaceCmd = "patchelf --replace-needed \"" + soname + "\" \"" + newDepName + "\" \"" + newPath.string() + "\"";

            std::cout << "    " << replaceCmd << std::endl;
            if(!m_dryRun) {
                Process::exec(replaceCmd);
            }
        }
    }

    // --- Phase 4: Verify the relocated library ---
    std::cout << "\nPhase 4: " << (m_dryRun ? "Planning to verify" : "Verifying") << " bundled library..." << std::endl;
    if (!m_dryRun) {
        auto canonicalInputPath = std::filesystem::canonical(inputFile);
        std::filesystem::path primaryLibInBundle = outputDir / canonicalInputPath.filename();

        std::cout << "  Attempting to dynamically load: " << primaryLibInBundle << std::endl;

        setenv("LD_DEBUG", "libs", 1);
        setenv("LD_LIBRARY_PATH", outputDir.c_str(), 1);

        void* handle = dlopen(primaryLibInBundle.c_str(), RTLD_LAZY);
        if (!handle) {
            std::cerr << "\n  VERIFICATION FAILED: dlopen reported an error:\n  " << dlerror() << std::endl;
        } else {
            std::cout << "\n  Verification SUCCESS: Library loaded successfully." << std::endl;
            dlclose(handle);
        }

        unsetenv("LD_DEBUG");
        unsetenv("LD_LIBRARY_PATH");
    } else {
        std::cout << "  Would set LD_DEBUG=libs and LD_LIBRARY_PATH, then test loading the main library with dlopen." << std::endl;
    }
}

