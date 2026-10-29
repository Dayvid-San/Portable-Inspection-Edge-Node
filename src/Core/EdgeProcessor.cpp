#include "EdgeProcessor.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <iostream>

inline uint8_t getPixel(const std::vector<uint8_t>& data, int w, int h, int x, int y) {
    if (x < 0) x = 0;
    if (x >= w) x = w - 1;
    if (y < 0) y = 0;
    if (y >= h) y = h - 1;
    return data[y * w + x];
}

AnalysisResult EdgeProcessor::analyze(const ImageFrame& frame) {
    int w = frame.width;
    int h = frame.height;
    
    std::vector<uint8_t> grayImage = frame.data; 

    std::vector<uint8_t> blurredImage(w * h);
    for (int y = 1; y < h - 1; y++) {
        for (int x = 1; x < w - 1; x++) {
            int sum = 0;
            sum += getPixel(grayImage, w, h, x-1, y-1) * 1;
            sum += getPixel(grayImage, w, h, x  , y-1) * 2;
            sum += getPixel(grayImage, w, h, x+1, y-1) * 1;
            sum += getPixel(grayImage, w, h, x-1, y  ) * 2;
            sum += getPixel(grayImage, w, h, x  , y  ) * 4;
            sum += getPixel(grayImage, w, h, x+1, y  ) * 2;
            sum += getPixel(grayImage, w, h, x-1, y+1) * 1;
            sum += getPixel(grayImage, w, h, x  , y+1) * 2;
            sum += getPixel(grayImage, w, h, x+1, y+1) * 1;
            blurredImage[y * w + x] = sum / 16;
        }
    }

    int edgePixelCount = 0;
    int threshold = 100;
    
    std::string visualMap;
    visualMap.reserve((w + 1) * h);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (y == 0 || y == h - 1 || x == 0 || x == w - 1) {
                visualMap += '.';
                continue;
            }

            int gx = -1 * getPixel(blurredImage, w, h, x-1, y-1) + 1 * getPixel(blurredImage, w, h, x+1, y-1)
                     -2 * getPixel(blurredImage, w, h, x-1, y  ) + 2 * getPixel(blurredImage, w, h, x+1, y  )
                     -1 * getPixel(blurredImage, w, h, x-1, y+1) + 1 * getPixel(blurredImage, w, h, x+1, y+1);

            int gy = -1 * getPixel(blurredImage, w, h, x-1, y-1) - 2 * getPixel(blurredImage, w, h, x  , y-1) - 1 * getPixel(blurredImage, w, h, x+1, y-1)
                     +1 * getPixel(blurredImage, w, h, x-1, y+1) + 2 * getPixel(blurredImage, w, h, x  , y+1) + 1 * getPixel(blurredImage, w, h, x+1, y+1);

            int magnitude = std::sqrt(gx*gx + gy*gy);
            
            if (magnitude > threshold) {
                edgePixelCount++;
                visualMap += '#';
            } else {
                visualMap += '.';
            }
        }
        visualMap += '\n';
    }

    float density = (float)edgePixelCount / (w * h);
    
    return { density, 0.95f, 0, visualMap };
}