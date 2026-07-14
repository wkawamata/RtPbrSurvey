# Third-Party Notices

RtPbrSurvey includes code and assets from third parties. The top-level
`LICENSE` applies to the application source unless a file or asset directory
states otherwise.

The bundled assets are included as free-use sample data for renderer
development, validation, and demonstration. They are not part of the MIT-licensed
application source, and they should not be assumed to be available for paid
application distribution unless the asset's own license explicitly permits that
use.

## Microsoft DirectX-Graphics-Samples

Portions of the application source are derived from Microsoft
DirectX-Graphics-Samples, which is licensed under the MIT License.

Source: https://github.com/microsoft/DirectX-Graphics-Samples

## glTF Sample Models

The repository includes several glTF sample assets for renderer validation.
Asset licensing is separate from the application source license.

| Asset | Local path | Download source | License status |
|-------|------------|-----------------|----------------|
| Avocado | `Assets/Models/Avocado/` | https://github.com/KhronosGroup/glTF-Sample-Models/tree/main/2.0/Avocado | CC0; see `Assets/Models/Avocado/README.md`. |
| BoomBox | `Assets/Models/BoomBox/` | https://github.com/KhronosGroup/glTF-Sample-Models/tree/main/2.0/BoomBox | CC0; see `Assets/Models/BoomBox/README.md`. |
| DamagedHelmet | `Assets/Models/DamagedHelmet/` | https://github.com/KhronosGroup/glTF-Sample-Models/tree/main/2.0/DamagedHelmet | Creative Commons Attribution-NonCommercial; see `Assets/Models/DamagedHelmet/LICENSE.txt`. |
| Lantern | `Assets/Models/Lantern/` | https://github.com/KhronosGroup/glTF-Sample-Models/tree/main/2.0/Lantern | Free sample asset for validation; verify upstream license before paid application distribution. |
| Sponza | `Assets/Models/Sponza/` | https://github.com/KhronosGroup/glTF-Sample-Models/tree/main/2.0/Sponza | Free sample asset for validation; verify upstream license before paid application distribution. |

## Environment Maps

The environment map files under `Assets/Environment/` need explicit source and
license documentation before they should be treated as paid application
distribution assets.

| Asset | Local path | Download source | License status |
|-------|------------|-----------------|----------------|
| Atrium IBL maps | `Assets/Environment/Atrium_diffuseIBL.dds`, `Assets/Environment/Atrium_specularIBL.dds` | Source URL needed before public release. | Free-use validation asset only until source and license are documented. |
| Default environment maps | `Assets/Environment/default_environment.dds`, `Assets/Environment/default_environment.hdr` | Source URL needed before public release. | Free-use validation asset only until source and license are documented. |

## Acknowledgements

Thanks to Microsoft for DirectX-Graphics-Samples, and to the Khronos Group and
the original glTF sample model authors for making practical renderer validation
assets available to the graphics community.

## Package Dependencies

NuGet and vcpkg dependencies are restored locally and are not committed.
Consult the corresponding package metadata after restore for each package's
license terms.
