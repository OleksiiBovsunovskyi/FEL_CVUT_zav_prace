module;
#include "pgr.h"

import postprocessshader;
import Logger;

export module PostProcessEffect;
/**
 * Base class for all post process effects 
 */
export class IPostProcessEffect {
public:
    virtual bool Init(int width, int height) = 0;
    virtual void operator()(GLuint quadVAO, int width, int height) = 0;
    virtual ~IPostProcessEffect() = default;

};