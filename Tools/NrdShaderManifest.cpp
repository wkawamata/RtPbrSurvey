#include <NRD.h>
#include <fstream>

int main(int argc, char** argv)
{
    if (argc != 2)
        return 1;
    const nrd::DenoiserDesc denoiser = {0, nrd::Denoiser::RELAX_DIFFUSE_SPECULAR};
    const nrd::InstanceCreationDesc creation = {{}, &denoiser, 1};
    nrd::Instance* instance = nullptr;
    if (nrd::CreateInstance(creation, instance) != nrd::Result::SUCCESS || instance == nullptr)
        return 2;
    const auto& description = *nrd::GetInstanceDesc(*instance);
    std::ofstream output(argv[1], std::ios::binary);
    for (uint32_t i = 0; i < description.pipelinesNum; ++i)
        output << description.pipelines[i].shaderIdentifier << '\n';
    const bool success = output.good();
    nrd::DestroyInstance(*instance);
    return success ? 0 : 3;
}
