
#ifndef GBUFFER_CB_H
#define GBUFFER_CB_H

#include "view_cb.h"

struct GBufferFillConstants
{
    PlanarViewConstants leftView;
    PlanarViewConstants leftViewPrev;
    PlanarViewConstants rightView;
    PlanarViewConstants rightViewPrev;
};

#endif // GBUFFER_CB_H