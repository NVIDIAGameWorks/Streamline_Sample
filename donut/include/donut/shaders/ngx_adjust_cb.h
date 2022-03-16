
#ifndef NGX_ADJUST_CB_H
#define NGX_ADJUST_CB_H

struct NGXAdjustmentConstants
{
    bool isHDR;
    float postExposureScale;
    float postBlackLevel;
    float postWhiteLevel;
    float postGamma;
};

#endif // NGX_ADJUST_CB_H