// src/Windows_Relocator.hpp
#ifndef RELOCATOR_WINDOWS_RELOCATOR_HPP
#define RELOCATOR_WINDOWS_RELOCATOR_HPP

#include "Relocator.hpp"

class Windows_Relocator : public Relocator {
public:
    using Relocator::Relocator;
    void bundleDependencies(
        const std::filesystem::path& inputFile,
        const std::filesystem::path& outputDir,
        const std::vector<std::string>& searchPaths,
        const std::vector<std::string>& literalExclusions,
        const std::vector<std::string>& regexExclusions
    ) override;
};