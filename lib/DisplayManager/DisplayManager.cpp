/*
 * DisplayManager.cpp
 * Implementation of multi-display management
 */

#include "DisplayManager.h"

// DisplayInstance implementation
DisplayInstance::DisplayInstance(const DisplayConfig& cfg) 
    : config(cfg), tft(nullptr), initialized(false),
      imageFrameEnabled(false), imageFrameColor(ST77XX_WHITE), 
      imageFrameThickness(1), frameBuffer(nullptr), frameBufferAllocated(false) {
}

DisplayInstance::~DisplayInstance() {
    if (tft) {
        delete tft;
    }
    if (frameBuffer) {
        delete[] frameBuffer;
    }
}

bool DisplayInstance::initialize() {
    // Already initialized - return success
    if (initialized) {
        return true;
    }
    
    // Validate config before initializing
    if (config.cs == 0 || config.dc == 0 || config.rst == 0) {
        return false;  // Invalid pin configuration
    }
    
    // Create TFT instance
    tft = new Adafruit_ST7735(config.cs, config.dc, config.rst);
    if (!tft) {
        return false;  // Memory allocation failed
    }
    
    // Initialize backlight control
    pinMode(config.bl, OUTPUT);
    digitalWrite(config.bl, HIGH);  // Turn on backlight
    
    // Initialize display hardware
    tft->initR(INITR_BLACKTAB);
    tft->setRotation(config.rotation);
    
    initialized = true;
    return true;
}

void DisplayInstance::showTestPattern() {
    if (!tft || !initialized) {
        return;
    }
    
    // Clear screen
    tft->fillScreen(ST77XX_BLACK);
    
    // Draw gradient FIRST (background)
    drawColorBars();
    
    // Draw calibration frame SECOND (on top of gradient)
    drawCalibrationFrame();
    
    // Draw device info LAST (on top of everything)
    drawDeviceInfo();
}

void DisplayInstance::clear() {
    if (tft && initialized) {
        tft->fillScreen(ST77XX_BLACK);
    }
}

void DisplayInstance::setBacklight(bool on) {
    digitalWrite(config.bl, on ? HIGH : LOW);
}

void DisplayInstance::drawCalibrationFrame(int8_t adjustTop, int8_t adjustBottom,
                                          int8_t adjustLeft, int8_t adjustRight,
                                          uint16_t frameColor, uint8_t frameThickness) {
    // Clear screen first to remove old frame
    tft->fillScreen(ST77XX_BLACK);
    
    // Draw frame with specified color, thickness, and adjustments
    drawImageFrame(frameColor, frameThickness, 
                  adjustTop, adjustBottom, adjustLeft, adjustRight);
    
    // Draw diagonal from origin to display center (identifies origin)
    // Use runtime TFT dimensions instead of config to avoid coordinate mismatches
    int displayCenterX = tft->width() / 2;
    int displayCenterY = tft->height() / 2;
    
    // Draw thicker diagonal line to prevent single-pixel gaps
    tft->drawLine(0, 0, displayCenterX, displayCenterY, ST77XX_YELLOW);
    // Add parallel line for thickness (offset by 1 pixel if within bounds)
    if (displayCenterX > 0 && displayCenterY > 0) {
        tft->drawLine(1, 0, displayCenterX, displayCenterY - 1, ST77XX_YELLOW);
    }
    
    // Mark origin
    tft->drawPixel(0, 0, ST77XX_WHITE);
    tft->drawPixel(1, 0, ST77XX_WHITE);
    tft->drawPixel(0, 1, ST77XX_WHITE);
    
    // Mark calibrated center with red cross
    tft->drawPixel(config.centerX, config.centerY, ST77XX_RED);
    tft->drawPixel(config.centerX-1, config.centerY, ST77XX_RED);
    tft->drawPixel(config.centerX+1, config.centerY, ST77XX_RED);
    tft->drawPixel(config.centerX, config.centerY-1, ST77XX_RED);
    tft->drawPixel(config.centerX, config.centerY+1, ST77XX_RED);
}

void DisplayInstance::drawColorBars() {
    // Draw gradient background across entire usable area
    // Horizontal gradient: blue -> cyan -> green -> yellow -> red
    
    const int startX = config.usableX;
    const int endX = config.usableX + config.usableWidth;
    const float widthInv = 1.0f / config.usableWidth;  // Precompute division
    const float pi = 3.14159f;
    
    for (int x = startX; x < endX; x++) {
        // Calculate normalized position [0.0 to 1.0]
        const float ratio = (x - startX) * widthInv;
        
        // Generate smooth color transition
        const uint8_t r = (uint8_t)(ratio * 255.0f);
        const uint8_t g = (uint8_t)(128.0f + 127.0f * sin(ratio * pi));
        const uint8_t b = (uint8_t)((1.0f - ratio) * 255.0f);
        
        // Convert RGB888 to RGB565 format
        const uint16_t color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        
        // Draw vertical line for this color
        tft->drawFastVLine(x, config.usableY, config.usableHeight, color);
    }
}

void DisplayInstance::drawDeviceInfo() {
    tft->setTextColor(ST77XX_BLACK);
    tft->setTextSize(2);  // Doubled from 1 to 2
    tft->setTextWrap(false);
    
    // Position text in upper area
    int textY = config.usableY + 5;
    int textX = config.usableX + 5;
    
    tft->setCursor(textX, textY);
    tft->print(config.name);
    
    tft->setCursor(textX, textY + 20);  // Increased spacing from 10 to 20 for larger text
    tft->print(config.width);
    tft->print("x");
    tft->print(config.height);
}

bool DisplayInstance::isWithinBounds(int x, int y) const {
    return (x >= 0 && x < config.width &&
            y >= 0 && y < config.height);
}

bool DisplayInstance::isWithinFrameBounds(int x, int y,
                                         int8_t adjustTop, int8_t adjustBottom,
                                         int8_t adjustLeft, int8_t adjustRight) const {
    // Calculate frame boundaries with adjustments
    // Remember: top and left use inverted calculations
    int16_t frameTop = config.usableY - adjustTop;
    int16_t frameLeft = config.usableX - adjustLeft;
    int16_t frameBottom = (config.usableY + config.usableHeight - 1) + adjustBottom;
    int16_t frameRight = (config.usableX + config.usableWidth - 1) + adjustRight;
    
    // Check if pixel is within the frame boundaries
    return (x >= frameLeft && x <= frameRight &&
            y >= frameTop && y <= frameBottom);
}

void DisplayInstance::drawImageFrame(uint16_t color, uint8_t thickness,
                                    int8_t adjustTop, int8_t adjustBottom,
                                    int8_t adjustLeft, int8_t adjustRight) {
    if (!tft || !initialized) {
        return;
    }
    
    // Apply adjustments to usable area boundaries (relative to config values)
    // Adjustments move edges: positive = outward (expand), negative = inward (shrink)
    // Top/Left: positive decreases coordinate (moves up/left), negative increases (moves down/right)
    // Bottom/Right: positive increases coordinate (moves down/right), negative decreases (moves up/left)
    int16_t top = config.usableY - adjustTop;      // Inverted: +adjustTop moves edge UP
    int16_t bottom = (config.usableY + config.usableHeight - 1) + adjustBottom;
    int16_t left = config.usableX - adjustLeft;    // Inverted: +adjustLeft moves edge LEFT
    int16_t right = (config.usableX + config.usableWidth - 1) + adjustRight;
    
    // Calculate dimensions from adjusted edges
    int16_t x = left;
    int16_t y = top;
    int16_t w = right - left + 1;
    int16_t h = bottom - top + 1;
    
    // Bounds checking to prevent crashes
    if (x < -10) x = -10;  // Allow 10 pixels beyond display
    if (y < -10) y = -10;
    if (w <= 0 || h <= 0) return;  // Nothing to draw
    
    // Draw frame with specified thickness
    // Draw inward from the boundary (like cal_lcd.cpp reference implementation)
    for (uint8_t i = 0; i < thickness; i++) {
        // Calculate rectangle for this thickness layer
        int16_t fx = x + i;
        int16_t fy = y + i;
        int16_t fw = w - (2 * i);
        int16_t fh = h - (2 * i);
        
        // Stop if no space left for this layer
        if (fw <= 0 || fh <= 0) break;
        
        // Draw rectangle (allow slightly beyond display bounds for calibration)
        tft->drawRect(fx, fy, fw, fh, color);
    }
}

void DisplayInstance::clearImageFrame() {
    if (!tft || !initialized) {
        return;
    }
    
    // Clear frame by drawing in black
    int16_t x = config.usableX;
    int16_t y = config.usableY;
    int16_t w = config.usableWidth;
    int16_t h = config.usableHeight;
    
    for (uint8_t i = 0; i < imageFrameThickness; i++) {
        tft->drawRect(x - i, y - i, w + 2*i, h + 2*i, ST77XX_BLACK);
    }
}

void DisplayInstance::enableImageFrame(bool enable, uint16_t color, uint8_t thickness,
                                      int8_t adjustTop, int8_t adjustBottom,
                                      int8_t adjustLeft, int8_t adjustRight) {
    imageFrameEnabled = enable;
    imageFrameColor = color;
    imageFrameThickness = thickness;
    
    if (enable) {
        drawImageFrame(color, thickness, adjustTop, adjustBottom, adjustLeft, adjustRight);
    } else {
        clearImageFrame();
    }
}

// DisplayManager implementation

// DisplayManager implementation
DisplayManager::DisplayManager() : displayCount(0) {
    for (uint8_t i = 0; i < MAX_DISPLAYS; i++) {
        displays[i] = nullptr;
    }
}

DisplayManager::~DisplayManager() {
    for (uint8_t i = 0; i < displayCount; i++) {
        if (displays[i]) {
            delete displays[i];
        }
    }
}

bool DisplayManager::addDisplay(const DisplayConfig& config) {
    // Check capacity
    if (displayCount >= MAX_DISPLAYS) {
        return false;  // Maximum displays reached
    }
    
    // Validate config
    if (!config.name || config.width == 0 || config.height == 0) {
        return false;  // Invalid configuration
    }
    
    // Check for duplicate names
    for (uint8_t i = 0; i < displayCount; i++) {
        if (displays[i] && strcmp(displays[i]->getName(), config.name) == 0) {
            return false;  // Display with this name already exists
        }
    }
    
    // Create display instance
    displays[displayCount] = new DisplayInstance(config);
    if (!displays[displayCount]) {
        return false;  // Memory allocation failed
    }
    
    displayCount++;
    return true;
}

bool DisplayManager::initializeAll() {
    bool allSuccess = true;
    
    for (uint8_t i = 0; i < displayCount; i++) {
        if (displays[i] && !displays[i]->initialize()) {
            allSuccess = false;
        }
    }
    
    return allSuccess;
}

void DisplayManager::showAllTestPatterns() {
    for (uint8_t i = 0; i < displayCount; i++) {
        if (displays[i]) {
            displays[i]->showTestPattern();
        }
    }
}

DisplayInstance* DisplayManager::getDisplay(const char* name) {
    for (uint8_t i = 0; i < displayCount; i++) {
        if (displays[i] && strcmp(displays[i]->getName(), name) == 0) {
            return displays[i];
        }
    }
    return nullptr;
}

DisplayInstance* DisplayManager::getDisplay(uint8_t index) {
    if (index < displayCount) {
        return displays[index];
    }
    return nullptr;
}

void DisplayManager::listDisplays(Stream& serial) {
    serial.println("Registered displays:");
    for (uint8_t i = 0; i < displayCount; i++) {
        if (displays[i]) {
            const DisplayConfig& cfg = displays[i]->getConfig();
            serial.print("  [");
            serial.print(i);
            serial.print("] ");
            serial.print(cfg.name);
            serial.print(" - ");
            serial.print(cfg.width);
            serial.print("x");
            serial.print(cfg.height);
            serial.print(" (");
            serial.print(cfg.manufacturer);
            serial.println(")");
        }
    }
}
