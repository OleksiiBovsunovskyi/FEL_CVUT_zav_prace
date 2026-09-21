#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

import Logger;
import GltfImporter;
import PmmaFile;

/* Bakes one .gltf or .glb into the .pmma the renderer loads. Driven by the
 * build, which rebakes an asset whose source is newer than its .pmma. */
int main(int argc, char** argv) {
    try {
        if (argc != 3) {
            std::fprintf(stderr, "usage: pgr-bake <model.gltf|model.glb> <model.pmma>\n");
            return EXIT_FAILURE;
        }
        const std::filesystem::path source{argv[1]};
        const std::filesystem::path baked{argv[2]};

        ImportedAsset asset;
        if (!importGltf(source, {}, asset)) return EXIT_FAILURE;
        if (!writePmma(baked, asset.view())) return EXIT_FAILURE;

        logMessage("pgr-bake: " + source.filename().string() + " -> " +
                   baked.filename().string());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fatal: %s\n", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
