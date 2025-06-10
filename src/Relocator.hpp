#ifndef RELOCATOR_RELOCATOR_HPP
#define RELOCATOR_RELOCATOR_HPP

#include <filesystem>
#include <string>
#include <vector>

/**
 * @class Relocator
 * @brief Abstract base class defining the interface for a platform-specific dependency bundler.
 */
class Relocator {
public:
    explicit Relocator(bool dryRun) : m_dryRun(dryRun) {}
    virtual ~Relocator() = default;

    /**
     * The main entry point. Analyzes an input file, finds its dependencies,
     * copies them to an output directory, and fixes their linkage.
     * @param inputFile The primary executable or library to process.
     * @param outputDir The directory where dependencies will be copied.
     * @param searchPaths A list of additional directories to search for libraries.
     */
    virtual void bundleDependencies(
            const std::filesystem::path& inputFile,
            const std::filesystem::path& outputDir,
            const std::vector<std::string>& searchPaths
    ) = 0;

protected:
    bool m_dryRun;
};

#endif //RELOCATOR_RELOCATOR_HPP
