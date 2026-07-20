#pragma once

// Umbrella header of the software RHI backend. Including this (with `WITH_RHI_SOFTWARE` defined) pulls in
// the `RhiFwd.h` contract aliases plus the definitions of every `RHI::Software::Sw*` class the pipeline
// drives through the `nCine::RHI::` names. This is the software counterpart of the OpenGL backend include
// block in `Rhi.h`; wiring it into `Rhi.h` (and adding `WITH_RHI_SOFTWARE` to a build config) is a
// separate step — keeping this header standalone means the OpenGL build stays byte-for-byte unaffected.

#include "../RhiTypes.h"
#include "../RhiFwd.h"

#include "SwDebug.h"
#include "SwShader.h"
#include "SwBuffer.h"
#include "SwTexture.h"
#include "SwVertexFormat.h"
#include "SwShaderTypes.h"
#include "SwUniformCache.h"
#include "SwShaderUniforms.h"
#include "SwShaderProgram.h"
#include "SwRenderTarget.h"
#include "SwRaster.h"
#include "SwDevice.h"
