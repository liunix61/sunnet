﻿//
// sunnet project
// Copyright (C) 2018 by Contributors <https://github.com/Tyill/sunnet>
//
// This code is licensed under the MIT License.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "../stdafx.h"
#include "snOperatorCPU/src/Operator/pooling.h"
#include <thread>

using namespace std;
using namespace SN_Base;

void Pooling::forwardCPU(const poolParams& poolPrms, const snSize& insz, const snFloat* input,
    const snSize& outsz, snFloat* output, size_t* outputInx){

    size_t inStepByD = insz.w * insz.h,           // step in by input
        inStepByN = inStepByD * insz.d,        // step in by batch
        outStepByD = outsz.w * outsz.h,        // step out by output
        outStepByN = outStepByD * outsz.d,     // step out by batch
        stride = poolPrms.stride,
        kernel = poolPrms.kernel,
        kernelSz = kernel * kernel;
   
    size_t* shareI = (size_t*)calloc(insz.d * insz.n, sizeof(size_t));
    snFloat* shareF = (snFloat*)calloc(insz.d * insz.n, sizeof(snFloat));
  
    auto core = std::thread::hardware_concurrency();
    if (core == 0) core = 4;

    if (poolPrms.type == poolType::max){ // max

        // by batch
#pragma omp parallel for num_threads(core)
        for (int n = 0; n < int(insz.n); ++n){

            snFloat* outBuff = shareF + insz.d * n;
            size_t* outInxBuff = shareI + insz.d * n;

            for (size_t p = 0; p < outStepByD; ++p){

                size_t ox = p % outsz.w, oy = p / outsz.w,
                    posW = ox * stride, posH = oy * stride;

                memset(outBuff, 0, insz.d * sizeof(snFloat));
                memset(outInxBuff, 0, insz.d * sizeof(size_t));

                // kernel
                for (size_t c = 0; c < kernelSz; ++c){

                    size_t cx = c % kernel, cy = c / kernel;
                    const snFloat* pIn = input + (cx + posW) + (cy + posH) * insz.w + n * inStepByN;

                    // on all input layers
                    for (size_t d = 0; d < insz.d; ++d){
                        snFloat val = *pIn;
                        pIn += inStepByD;
                        if (val > outBuff[d]){
                            outBuff[d] = val;
                            outInxBuff[d] = c;
                        }
                    }
                }

                snFloat* pOut = output + ox + oy * outsz.w + n * outStepByN;
                size_t* pOutInx = outputInx + ox + oy * outsz.w + n * outStepByN;

                // on all output layers
                for (size_t k = 0; k < outsz.d; ++k){

                    *pOut = outBuff[k];
                    *pOutInx = outInxBuff[k];

                    pOut += outStepByD;
                    pOutInx += outStepByD;
                }
            }
        }
    }
    else{ // mean

        // by batch
#pragma omp parallel for num_threads(core)
        for (int n = 0; n < int(insz.n); ++n){

            snFloat* outBuff = shareF + insz.d * n;
          
            for (size_t p = 0; p < outStepByD; ++p){

                size_t ox = p % outsz.w, oy = p / outsz.w,
                    posW = ox * stride, posH = oy * stride;

                memset(outBuff, 0, insz.d * sizeof(snFloat));
              
                // kernel
                for (size_t c = 0; c < kernelSz; ++c){

                    size_t cx = c % kernel, cy = c / kernel;
                    const snFloat* pIn = input + (cx + posW) + (cy + posH) * insz.w + n * inStepByN;

                    // on all input layers
                    for (size_t d = 0; d < insz.d; ++d){
                        outBuff[d] += *pIn;
                        pIn += inStepByD;
                    }
                }

                snFloat* pOut = output + ox + oy * outsz.w + n * outStepByN;

                // on all output layers
                for (size_t k = 0; k < outsz.d; ++k){
                    *pOut = outBuff[k] / kernelSz;
                    pOut += outStepByD;
                }
            }
        }
    }
   
    free(shareI); 
    free(shareF);
}

void Pooling::backwardCPU(const poolParams& poolPrms, const snSize& outsz, const size_t* outputInx, const snFloat* gradIn, const snSize& insz, snFloat* gradOut){

    size_t inStepByD = insz.w * insz.h,        // step in by input
        inStepByN = inStepByD * insz.d,        // step in by batch
        outStepByD = outsz.w * outsz.h,        // step out by output
        outStepByN = outStepByD * outsz.d,     // step out by batch
        stride = poolPrms.stride,
        kernel = poolPrms.kernel,
        kernelSz = kernel * kernel;
       
    memset(gradOut, 0, inStepByN * insz.n * sizeof(snFloat));

    auto core = std::thread::hardware_concurrency();
    if (core == 0) core = 4;

    if (poolPrms.type == poolType::max){ // max

        // by batch
#pragma omp parallel for num_threads(core)
        for (int n = 0; n < int(insz.n); ++n){

            for (size_t p = 0; p < outStepByD; ++p){

                size_t ox = p % outsz.w, oy = p / outsz.w,
                    posW = ox * stride, posH = oy * stride;

                const size_t* pOutInx = outputInx + ox + oy * outsz.w + n * outStepByN;
                const snFloat* pGrIn = gradIn + ox + oy * outsz.w + n * outStepByN;
                snFloat* pGrOut = gradOut + n * inStepByN;
              
                // on all input layers
                for (size_t d = 0; d < insz.d; ++d){
                                        
                    size_t c = *pOutInx, cx = c % kernel, cy = c / kernel;
                    pGrOut[(cx + posW) + (cy + posH) * insz.w] = *pGrIn;

                    pGrIn += outStepByD;
                    pOutInx += outStepByD;
                    pGrOut += inStepByD;
                }
            }
        }
    }
    else{ // mean

        size_t shareStepByN = insz.d;                 // for local mem
        snFloat* share = (snFloat*)calloc(shareStepByN * insz.n, sizeof(snFloat));

        // by batch
#pragma omp parallel for num_threads(core)
        for (int n = 0; n < int(insz.n); ++n){

            snFloat* outBuff = share + shareStepByN * n;

            for (size_t p = 0; p < outStepByD; ++p){

                size_t ox = p % outsz.w, oy = p / outsz.w,
                    posW = ox * stride, posH = oy * stride;

                const snFloat* pGrIn = gradIn + ox + oy * outsz.w + n * outStepByN;

                // on all output layers
                for (size_t k = 0; k < outsz.d; ++k){
                    outBuff[k] = *pGrIn;
                    pGrIn += outStepByD;
                }

                // kernel
                for (size_t c = 0; c < kernelSz; ++c){

                    size_t cx = c % kernel, cy = c / kernel;
                    snFloat* pGrOut = gradOut + (cx + posW) + (cy + posH) * insz.w + n * inStepByN;

                    // on all input layers
                    for (size_t d = 0; d < insz.d; ++d){
                        *pGrOut = outBuff[d] / kernelSz;
                        pGrOut += inStepByD;
                    }
                }
            }
        }

        free(share);
    }
}