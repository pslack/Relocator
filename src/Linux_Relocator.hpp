// src/Linux_Relocator.hpp
#ifndef RELOCATOR_LINUX_RELOCATOR_HPP
#define RELOCATOR_LINUX_RELOCATOR_HPP

#include "Relocator.hpp"

class Linux_Relocator : public Relocator {
public:
    using Relocator::Relocator;
    void bundleDependencies(
            const std::filesystem::path& inputFile,
            const std::filesystem::path& outputDir,
            const std::vector<std::string>& searchPaths
    ) override;
};

#endif //RELOCATOR_LINUX_RELOCATOR_HPP
