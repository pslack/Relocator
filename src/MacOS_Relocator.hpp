
// src/MacOS_Relocator.hpp
#ifndef RELOCATOR_MACOS_RELOCATOR_HPP
#define RELOCATOR_MACOS_RELOCATOR_HPP

#include "Relocator.hpp"

class MacOS_Relocator : public Relocator {
public:
    using Relocator::Relocator; // Inherit constructor

    void bundleDependencies(
            const std::filesystem::path& inputFile,
            const std::filesystem::path& outputDir,
            const std::vector<std::string>& searchPaths
    ) override;
};

#endif //RELOCATOR_MACOS_RELOCATOR_HPP




