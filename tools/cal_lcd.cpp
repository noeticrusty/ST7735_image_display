/*
 * ST7735 Display Calibration Tool
 * Interactive tool to identify display bounds, origin, and usable area
 * Exports calibration data as TOML .config files
 * 
 * TO USE THIS CALIBRATION TOOL:
 * 1. Copy this file to src/main.cpp (backup the original main.cpp first)
 * 2. Build and upload to Arduino Due
 * 3. Use serial monitor at 115200 baud to run calibration commands
 * 4. Use 'bounds' command to set calibration values
 * 5. Use 'export' command to generate .config file
 * 6. Copy/paste output to save as <DeviceName>.config
 * 7. Run: python3 generate_config_header.py --device <DeviceName>
 * 8. Restore the original main.cpp
 * 
 * Default wiring (modify pin definitions below for your setup):
 * VCC -> 3.3V
 * GND -> GND
 * CS  -> Pin 7
 * RST -> Pin 8  
 * DC  -> Pin 10
 * SDA -> Pin 11 (MOSI)
 * SCK -> Pin 13 (SCK)
 * BL  -> Pin 9 (backlight)
 * 
 * Workflow:
 * 1. Run 'frame' to see display boundaries
 * 2. Run 'bounds L,R,T,B' with observed values (e.g., 'bounds 1,158,2,127')
 * 3. Run 'center' to verify center point
 * 4. Run 'export' to generate .config file
 * 5. Copy/paste output and save to file
 */

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>

// ST7735 pin definitions - DueLCD01 configuration
#define TFT_CS     7      // Chip Select
#define TFT_DC     10     // Data/Command select
#define TFT_RST    8      // Reset
#define TFT_BL     9      // Backlight control

// Create ST7735 instance
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// Published display dimensions (from .config files)
// These are the nominal display dimensions that manufacturers publish
// NOTE: Standard ST7735 1.8" displays are 160x128 in landscape orientation
// If calibrating a different size display, update these constants before compilation
const int PUBLISHED_WIDTH = 160;
const int PUBLISHED_HEIGHT = 128;

// Calibration variables
int currentRotation = 1;  // Default to landscape
int usableOriginX = 0;
int usableOriginY = 0;
int usableWidth = 0;
int usableHeight = 0;
int frameThickness = 2;  // Frame thickness (1-5)

// Mode selection (1-6, plus NO_MODE state)
enum CalibrationMode {
  MODE_NONE = -1,          // Not in any mode
  MODE_EDGE_ADJUST = 0,    // Mode 1
  MODE_FRAME_MOVE = 1,     // Mode 2
  MODE_THICKNESS = 2,      // Mode 3
  MODE_ROTATION = 3,       // Mode 4
  MODE_SAVE_EXIT = 4,      // Mode 5
  MODE_EXIT_NO_SAVE = 5    // Mode 6
};
CalibrationMode currentMode = MODE_NONE;  // Start with no mode active

// Change tracking for save verification
struct SavedState {
  int rotation;
  int usableOriginX;
  int usableOriginY;
  int usableWidth;
  int usableHeight;
  int frameThickness;
};
SavedState lastSavedState = {1, 0, 0, 0, 0, 2};
bool hasUnsavedChanges = false;
bool hasEverSaved = false;

// Display configuration
String currentDisplayName = "";  // Name of display being calibrated
bool configExists = false;       // Whether a valid config exists

// Function prototypes
void showHelp();
void setRotation(int rotation);
void drawFrame();
void clearScreen();
void drawOriginToCenterLine();
void runCalibrationTest();
void drawUsableCenter();
void processCommand(String command);
void showDisplayInfo();
void waitForKeypress();
void exportConfig();
void setUsableBounds(int left, int right, int top, int bottom);

// New functions for arrow key control
void handleArrowKey(char key);
void setMode(int mode);
void showModePrompt();
void markModified();
bool checkUnsavedChanges();
void saveAndExit();
void exitWithoutSaving();
void adjustEdge(char direction);
void moveFrame(char direction);
void adjustThickness(char direction);
void rotateDisplay(char direction);
void redrawFrame();
void initializeBoundsFromPublished();
void handleEscapeKey();
bool validateAndClampBounds();
void selectOrCreateDisplay();
void createNewDisplayConfig();
String readSerialLine();
void showStartupMenu();

void setup() {
  // Initialize serial communication
  SerialUSB.begin(115200);
  while (!Serial) {
    ; // Wait for serial port to connect
  }
  
  // Initialize backlight control
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);  // Turn on backlight
  
  // Initialize SPI
  SPI.begin();
  
  // Initialize ST7735 display
  tft.initR(INITR_BLACKTAB);   // Initialize ST7735S chip, black tab
  
  // Set default rotation (no auto display)
  currentRotation = 1;
  tft.setRotation(currentRotation);
  
  // Clear screen only
  tft.fillScreen(ST77XX_BLACK);
  
  // Show welcome message
  SerialUSB.println();
  SerialUSB.println("========================================");
  SerialUSB.println("ST7735 Display Calibration Tool v2.0");
  SerialUSB.println("========================================");
  SerialUSB.println();
  
  // Display selection / creation menu
  selectOrCreateDisplay();
  
  // Initialize bounds from published dimensions
  initializeBoundsFromPublished();
  
  // Initialize saved state
  lastSavedState.usableOriginX = 0;
  lastSavedState.usableOriginY = 0;
  lastSavedState.usableWidth = 0;
  lastSavedState.usableHeight = 0;
  lastSavedState.rotation = currentRotation;
  lastSavedState.frameThickness = frameThickness;
  
  // Show help
  SerialUSB.println();
  SerialUSB.println("Connected! Ready for calibration.");
  SerialUSB.println();
  showHelp();
}

void showHelp() {
  SerialUSB.println("========== Arrow Key Calibration Mode ==========");
  SerialUSB.println();
  SerialUSB.println("Display: " + currentDisplayName);
  SerialUSB.println();
  SerialUSB.println("QUICK START:");
  SerialUSB.println("  Initial bounds loaded from published dimensions");
  SerialUSB.println("  1. Type 'info' to see current settings");
  SerialUSB.println("  2. Press '1' then use arrow keys to fine-tune");
  SerialUSB.println("  3. Press '5' when done to save & export");
  SerialUSB.println();
  SerialUSB.println("MODE SELECTION (Press 1-6):");
  SerialUSB.println("  1 - Adjust Frame Edges    (arrow keys expand/contract, ESC to exit mode)");
  SerialUSB.println("  2 - Move Entire Frame     (arrow keys shift position, ESC to exit mode)");
  SerialUSB.println("  3 - Adjust Thickness      (up/down = 1-5px, ESC to exit mode)");
  SerialUSB.println("  4 - Rotate Display        (left/right = CCW/CW, ESC to exit mode)");
  SerialUSB.println("  5 - Save & Exit           (export .config, ESC to cancel)");
  SerialUSB.println("  6 - Exit Without Saving   (ESC to cancel)");
  SerialUSB.println();
  SerialUSB.println("ARROW KEYS:");
  SerialUSB.println("  ↑ ↓ ← → - Adjust based on current mode");
  SerialUSB.println();
  SerialUSB.println("SPECIAL KEYS:");
  SerialUSB.println("  ESC    - Exit current mode (1-4) or trigger save sequence (no mode)");
  SerialUSB.println("  Ctrl-C - Quick save & exit");
  SerialUSB.println();
  SerialUSB.println("LEGACY TEXT COMMANDS:");
  SerialUSB.println("  rot0-3, frame, clear, cross, test, center");
  SerialUSB.println("  bounds L,R,T,B, export, info, help");
  SerialUSB.println();
  SerialUSB.println("================================================");
  SerialUSB.println();
}

void showDisplayInfo() {
  SerialUSB.println("Current Display Information:");
  SerialUSB.println("  Rotation: " + String(currentRotation));
  SerialUSB.println("  Nominal Width: " + String(tft.width()));
  SerialUSB.println("  Nominal Height: " + String(tft.height()));
  if (usableWidth > 0) {
    SerialUSB.println("  Usable Origin: (" + String(usableOriginX) + ", " + String(usableOriginY) + ")");
    SerialUSB.println("  Usable Size: " + String(usableWidth) + " x " + String(usableHeight));
  } else {
    SerialUSB.println("  Usable bounds: Not yet set (use 'bounds' command)");
  }
  SerialUSB.println("  Frame Thickness: " + String(frameThickness) + "px");
  
  SerialUSB.print("  Current Mode: ");
  switch(currentMode) {
    case MODE_NONE: SerialUSB.println("None (press 1-6 to select)"); break;
    case MODE_EDGE_ADJUST: SerialUSB.println("1 - Edge Adjust"); break;
    case MODE_FRAME_MOVE: SerialUSB.println("2 - Frame Move"); break;
    case MODE_THICKNESS: SerialUSB.println("3 - Thickness"); break;
    case MODE_ROTATION: SerialUSB.println("4 - Rotation"); break;
    case MODE_SAVE_EXIT: SerialUSB.println("5 - Save & Exit"); break;
    case MODE_EXIT_NO_SAVE: SerialUSB.println("6 - Exit Without Save"); break;
  }
  
  SerialUSB.print("  Changes Status: ");
  if (hasUnsavedChanges) {
    SerialUSB.println("UNSAVED");
  } else if (hasEverSaved) {
    SerialUSB.println("Saved");
  } else {
    SerialUSB.println("No changes");
  }
  SerialUSB.println();
}

void setRotation(int rotation) {
  if (rotation >= 0 && rotation <= 3) {
    currentRotation = rotation;
    tft.setRotation(rotation);
    SerialUSB.println("Rotation set to: " + String(rotation));
    SerialUSB.println("Display size: " + String(tft.width()) + " x " + String(tft.height()));
    
    // Reset usable area when rotation changes
    usableOriginX = 0;
    usableOriginY = 0;
    usableWidth = 0;
    usableHeight = 0;
    
    SerialUSB.println("Use 'cross' command to see origin-to-center line.");
  } else {
    SerialUSB.println("Invalid rotation. Use 0-3.");
  }
}

void clearScreen() {
  tft.fillScreen(ST77XX_BLACK);
  SerialUSB.println("Screen cleared to black using fillScreen().");
}

void drawFrame() {
  // If usable bounds are set, draw frame at those bounds
  if (usableWidth > 0 && usableHeight > 0) {
    // Ensure bounds are within display limits
    int maxX = tft.width();
    int maxY = tft.height();
    
    // Clamp values to valid range
    int clampedX = constrain(usableOriginX, 0, maxX - 1);
    int clampedY = constrain(usableOriginY, 0, maxY - 1);
    int clampedW = constrain(usableWidth, 1, maxX - clampedX);
    int clampedH = constrain(usableHeight, 1, maxY - clampedY);
    
    // Draw frame with current thickness, ensuring it stays within bounds
    int maxThickness = min(frameThickness, min(clampedW / 2, clampedH / 2));
    for (int i = 0; i < maxThickness; i++) {
      int rectW = clampedW - (2 * i);
      int rectH = clampedH - (2 * i);
      if (rectW > 0 && rectH > 0) {
        tft.drawRect(
          clampedX + i, 
          clampedY + i, 
          rectW, 
          rectH, 
          ST77XX_WHITE
        );
      }
    }
    SerialUSB.print("Frame drawn at usable bounds with thickness ");
    SerialUSB.println(maxThickness);
    return;
  }
  
  // Legacy mode - step through insets
  SerialUSB.println("Frame test - stepping through insets. Press any key to continue between steps...");
  
  // Step 1: Nominal frame
  clearScreen();
  tft.drawRect(0, 0, tft.width(), tft.height(), ST77XX_WHITE);
  SerialUSB.println("Step 1: White frame at nominal bounds (0,0) to (" + String(tft.width()-1) + "," + String(tft.height()-1) + ")");
  SerialUSB.println("Press any key to continue...");
  waitForKeypress();
  
  // Step 2: Add 1-pixel inset
  tft.drawRect(1, 1, tft.width() - 2, tft.height() - 2, ST77XX_RED);
  SerialUSB.println("Step 2: Added red frame with 1-pixel inset");
  SerialUSB.println("Press any key to continue...");
  waitForKeypress();
  
  // Step 3: Add 2-pixel inset
  tft.drawRect(2, 2, tft.width() - 4, tft.height() - 4, ST77XX_GREEN);
  SerialUSB.println("Step 3: Added green frame with 2-pixel inset");
  SerialUSB.println("Press any key to continue...");
  waitForKeypress();
  
  // Step 4: Add 3-pixel inset
  tft.drawRect(3, 3, tft.width() - 6, tft.height() - 6, ST77XX_BLUE);
  SerialUSB.println("Step 4: Added blue frame with 3-pixel inset");
  SerialUSB.println("Examine which frames are fully visible to determine usable bounds.");
}

void drawOriginToCenterLine() {
  clearScreen();
  
  // Draw coordinate system
  int centerX = tft.width() / 2;
  int centerY = tft.height() / 2;
  
  // Draw line from origin (0,0) to center
  // Use thicker line to prevent single-pixel gaps
  tft.drawLine(0, 0, centerX, centerY, ST77XX_YELLOW);
  // Add parallel line for thickness (offset by 1 pixel if within bounds)
  if (centerX > 0 && centerY > 0) {
      tft.drawLine(1, 0, centerX, centerY - 1, ST77XX_YELLOW);
  }
  
  // Draw axes
  tft.drawLine(0, 0, tft.width() - 1, 0, ST77XX_BLUE);    // X-axis
  tft.drawLine(0, 0, 0, tft.height() - 1, ST77XX_BLUE);   // Y-axis
  
  // Mark origin
  tft.drawPixel(0, 0, ST77XX_WHITE);
  tft.drawPixel(1, 0, ST77XX_WHITE);
  tft.drawPixel(0, 1, ST77XX_WHITE);
  
  // Mark center
  tft.drawPixel(centerX, centerY, ST77XX_RED);
  tft.drawPixel(centerX-1, centerY, ST77XX_RED);
  tft.drawPixel(centerX+1, centerY, ST77XX_RED);
  tft.drawPixel(centerX, centerY-1, ST77XX_RED);
  tft.drawPixel(centerX, centerY+1, ST77XX_RED);
  
  SerialUSB.println("Origin-to-center test:");
  SerialUSB.println("  Origin (0,0): White pixels");
  SerialUSB.println("  Blue lines: X and Y axes from origin");
  SerialUSB.println("  Yellow line: Origin to nominal center");
  SerialUSB.println("  Red cross: Nominal center at (" + String(centerX) + "," + String(centerY) + ")");
  SerialUSB.println("Check if origin and axes are visible.");
}

void runCalibrationTest() {
  SerialUSB.println("Running complete calibration test...");
  SerialUSB.println("Press any key between each step to continue.");
  SerialUSB.println();
  
  // Step 1: Show info
  SerialUSB.println("=== STEP 1: Display Information ===");
  showDisplayInfo();
  SerialUSB.println("Press any key to continue...");
  waitForKeypress();
  
  // Step 2: Clear screen
  SerialUSB.println("=== STEP 2: Clear Screen Test ===");
  clearScreen();
  SerialUSB.println("Press any key to continue...");
  waitForKeypress();
  
  // Step 3: Test rotations
  SerialUSB.println("=== STEP 3: Rotation Test ===");
  for (int rot = 0; rot < 4; rot++) {
    SerialUSB.println("Testing rotation " + String(rot) + "...");
    setRotation(rot);
    SerialUSB.println("Press any key to continue to next rotation...");
    waitForKeypress();
  }
  
  // Step 4: Frame test
  SerialUSB.println("=== STEP 4: Frame Boundary Test ===");
  drawFrame();
  
  // Step 5: Center test
  SerialUSB.println("=== STEP 5: Usable Center Test ===");
  drawUsableCenter();
  
  SerialUSB.println();
  SerialUSB.println("=== CALIBRATION TEST COMPLETE ===");
  SerialUSB.println("Based on your observations, you can determine:");
  SerialUSB.println("  1. Which rotation works best for your setup");
  SerialUSB.println("  2. The actual usable origin coordinates");
  SerialUSB.println("  3. The actual usable display dimensions");
  SerialUSB.println("Use individual commands for fine-tuning.");
}

void drawUsableCenter() {
  if (usableWidth == 0 || usableHeight == 0) {
    SerialUSB.println("Usable area not defined. Please set it first.");
    SerialUSB.println("Example: After determining usable area, manually set:");
    SerialUSB.println("  usableOriginX = 1; usableOriginY = 2;");
    SerialUSB.println("  usableWidth = 158; usableHeight = 126;");
    SerialUSB.println("Then call this function again.");
    
    // For demonstration, use common ST7735 values
    usableOriginX = 1;
    usableOriginY = 2;
    usableWidth = tft.width() - 2;
    usableHeight = tft.height() - 3;
    
    SerialUSB.println("Using estimated values for demonstration:");
    showDisplayInfo();
  }
  
  clearScreen();
  
  // Calculate usable center
  int centerX = usableOriginX + usableWidth / 2;
  int centerY = usableOriginY + usableHeight / 2;
  
  // Draw red cross at usable center
  tft.drawLine(centerX - 5, centerY, centerX + 5, centerY, ST77XX_RED);  // Horizontal
  tft.drawLine(centerX, centerY - 5, centerX, centerY + 5, ST77XX_RED);  // Vertical
  
  // Draw usable area boundary
  tft.drawRect(usableOriginX, usableOriginY, usableWidth, usableHeight, ST77XX_GREEN);
  
  SerialUSB.println("Red cross drawn at usable center: (" + String(centerX) + "," + String(centerY) + ")");
  SerialUSB.println("Green rectangle shows usable area boundary.");
}

void waitForKeypress() {
  // Clear any pending input
  while (SerialUSB.available()) {
    SerialUSB.read();
  }
  
  // Wait for any key
  while (!SerialUSB.available()) {
    delay(50);
  }
  
  // Read the key (but don't process as command)
  SerialUSB.readString();
  SerialUSB.println();
}

void setUsableBounds(int left, int right, int top, int bottom) {
  usableOriginX = left;
  usableOriginY = top;
  usableWidth = right - left + 1;
  usableHeight = bottom - top + 1;
  
  SerialUSB.println("Usable bounds set:");
  SerialUSB.println("  Left: " + String(left) + ", Right: " + String(right));
  SerialUSB.println("  Top: " + String(top) + ", Bottom: " + String(bottom));
  SerialUSB.println("  Usable area: " + String(usableWidth) + "x" + String(usableHeight));
  SerialUSB.println("  Center: (" + String(left + usableWidth/2) + ", " + String(top + usableHeight/2) + ")");
}

void exportConfig() {
  if (usableWidth == 0 || usableHeight == 0) {
    SerialUSB.println("Error: Usable bounds not set. Use 'bounds' command first.");
    SerialUSB.println("Example: bounds 1,158,2,127");
    return;
  }
  
  // Determine orientation string
  String orientation = "landscape";
  if (currentRotation == 0) orientation = "portrait";
  else if (currentRotation == 1) orientation = "landscape";
  else if (currentRotation == 2) orientation = "reverse_portrait";
  else if (currentRotation == 3) orientation = "reverse_landscape";
  
  // Calculate center
  int centerX = usableOriginX + usableWidth / 2;
  int centerY = usableOriginY + usableHeight / 2;
  
  // Calculate right and bottom edges
  int rightEdge = usableOriginX + usableWidth - 1;
  int bottomEdge = usableOriginY + usableHeight - 1;
  
  SerialUSB.println();
  SerialUSB.println("========== BEGIN CONFIG FILE ==========");
  SerialUSB.println("# ST7735 Display Configuration - " + currentDisplayName);
  SerialUSB.println("# Format: TOML v1.0.0");
  SerialUSB.println("# Generated by cal_lcd.cpp v2.0");
  SerialUSB.println();
  SerialUSB.println("[device]");
  SerialUSB.println("name = \"" + currentDisplayName + "\"");
  SerialUSB.println("manufacturer = \"Unknown\"  # TODO: Set manufacturer");
  SerialUSB.println("model = \"Generic ST7735\"  # TODO: Set model");
  SerialUSB.println("published_resolution = [" + String(PUBLISHED_WIDTH) + ", " + String(PUBLISHED_HEIGHT) + "]");
  SerialUSB.println();
  SerialUSB.println("[pinout]");
  SerialUSB.println("# Arduino Due pin assignments");
  SerialUSB.println("rst = " + String(TFT_RST));
  SerialUSB.println("dc = " + String(TFT_DC));
  SerialUSB.println("cs = " + String(TFT_CS));
  SerialUSB.println("bl = " + String(TFT_BL));
  SerialUSB.println();
  SerialUSB.println("[calibration]");
  SerialUSB.println("orientation = \"" + orientation + "\"");
  SerialUSB.println("# Usable area bounds (0-indexed, inclusive)");
  SerialUSB.println("left = " + String(usableOriginX));
  SerialUSB.println("right = " + String(rightEdge));
  SerialUSB.println("top = " + String(usableOriginY));
  SerialUSB.println("bottom = " + String(bottomEdge));
  SerialUSB.println("# Calculated center point");
  SerialUSB.println("center = [" + String(centerX) + ", " + String(centerY) + "]");
  SerialUSB.println("=========== END CONFIG FILE ===========");
  SerialUSB.println();
  SerialUSB.println("SAVE INSTRUCTIONS:");
  SerialUSB.println("1. Copy the text between BEGIN/END markers");
  SerialUSB.println("2. Save as: " + currentDisplayName + ".config");
  SerialUSB.println("3. Place in project root directory");
  SerialUSB.println("4. Run: python3 generate_config_header.py --device " + currentDisplayName);
  SerialUSB.println();
}

void processCommand(String command) {
  command.trim();
  String commandLower = command;
  commandLower.toLowerCase();
  
  bool showHelpAfter = true;  // Show help after most commands
  
  if (commandLower == "rot0") {
    setRotation(0);
  } else if (commandLower == "rot1") {
    setRotation(1);
  } else if (commandLower == "rot2") {
    setRotation(2);
  } else if (commandLower == "rot3") {
    setRotation(3);
  } else if (commandLower == "frame") {
    drawFrame();
  } else if (commandLower == "clear") {
    clearScreen();
  } else if (commandLower == "cross") {
    drawOriginToCenterLine();
  } else if (commandLower == "test") {
    runCalibrationTest();
    showHelpAfter = false;  // Don't show help after test (it shows its own conclusion)
  } else if (commandLower == "center") {
    drawUsableCenter();
  } else if (commandLower.startsWith("bounds ")) {
    // Parse bounds command: "bounds L,R,T,B"
    String params = command.substring(7);
    int commaPos1 = params.indexOf(',');
    int commaPos2 = params.indexOf(',', commaPos1 + 1);
    int commaPos3 = params.indexOf(',', commaPos2 + 1);
    
    if (commaPos1 > 0 && commaPos2 > 0 && commaPos3 > 0) {
      int left = params.substring(0, commaPos1).toInt();
      int right = params.substring(commaPos1 + 1, commaPos2).toInt();
      int top = params.substring(commaPos2 + 1, commaPos3).toInt();
      int bottom = params.substring(commaPos3 + 1).toInt();
      setUsableBounds(left, right, top, bottom);
    } else {
      SerialUSB.println("Error: Invalid bounds format. Use: bounds L,R,T,B");
      SerialUSB.println("Example: bounds 1,158,2,127");
    }
  } else if (commandLower == "export") {
    exportConfig();
    showHelpAfter = false;  // Export shows its own instructions
  } else if (commandLower == "info") {
    showDisplayInfo();
  } else if (commandLower == "help") {
    showHelp();
    showHelpAfter = false;  // Already showed help
  } else if (command.length() > 0) {
    SerialUSB.println("Unknown command: " + command);
    SerialUSB.println("Type 'help' for available commands.");
    showHelpAfter = false;
  } else {
    showHelpAfter = false;  // Empty command, don't show help
  }
  
  // Show help menu after command completion (except for test and help commands)
  if (showHelpAfter) {
    SerialUSB.println();
    SerialUSB.println("--- Command completed. Available commands: ---");
    showHelp();
  }
}

// ==================== New Arrow Key Control Functions ====================

void initializeBoundsFromPublished() {
  // Initialize bounds based on published dimensions and current rotation
  // This provides a reasonable starting point for calibration
  
  if (currentRotation == 1 || currentRotation == 3) {
    // Landscape orientations - use published width x height
    usableOriginX = 0;
    usableOriginY = 0;
    usableWidth = PUBLISHED_WIDTH;
    usableHeight = PUBLISHED_HEIGHT;
  } else {
    // Portrait orientations - swap dimensions
    usableOriginX = 0;
    usableOriginY = 0;
    usableWidth = PUBLISHED_HEIGHT;
    usableHeight = PUBLISHED_WIDTH;
  }
  
  SerialUSB.println("Initial bounds set from published dimensions:");
  SerialUSB.println("  Origin: (" + String(usableOriginX) + ", " + String(usableOriginY) + ")");
  SerialUSB.println("  Size: " + String(usableWidth) + " x " + String(usableHeight));
  SerialUSB.println("  Use arrow keys in Mode 1 to fine-tune edges");
}

void markModified() {
  hasUnsavedChanges = true;
}

void handleEscapeKey() {
  // ESC key behavior depends on current mode:
  // - Modes 1-4 (adjustment modes): Exit mode back to no-mode state
  // - No active mode: Initiate save & exit sequence
  // - Modes 5-6 are handled within their own functions (cancel operation)
  
  if (currentMode >= MODE_EDGE_ADJUST && currentMode <= MODE_ROTATION) {
    // In adjustment modes 1-4: exit the mode
    SerialUSB.println();
    SerialUSB.println("Exiting mode. Press 1-6 to select a new mode, or ESC to save & exit.");
    currentMode = MODE_NONE;  // Back to no-mode state
  } else if (currentMode == MODE_NONE) {
    // Not in an adjustment mode: trigger save & exit sequence
    SerialUSB.println();
    SerialUSB.println("ESC pressed - initiating save & exit sequence...");
    saveAndExit();
  }
  // MODE_SAVE_EXIT and MODE_EXIT_NO_SAVE handle ESC internally via checkUnsavedChanges()
}

void showModePrompt() {
  SerialUSB.println();
  SerialUSB.println("========== MODE SELECTION ==========");
  SerialUSB.println("1. Adjust Frame Edges (arrow keys move frame inward/outward)");
  SerialUSB.println("2. Move Entire Frame (arrow keys shift whole frame)");
  SerialUSB.println("3. Adjust Frame Thickness (up/down = thicker/thinner, 1-5px)");
  SerialUSB.println("4. Rotate Display (left/right = rotate CCW/CW)");
  SerialUSB.println("5. Save & Exit (save calibration to .config)");
  SerialUSB.println("6. Exit Without Saving");
  SerialUSB.println();
  SerialUSB.print("Current Mode: ");
  switch(currentMode) {
    case MODE_EDGE_ADJUST: SerialUSB.println("1 - Edge Adjust"); break;
    case MODE_FRAME_MOVE: SerialUSB.println("2 - Frame Move"); break;
    case MODE_THICKNESS: SerialUSB.println("3 - Thickness"); break;
    case MODE_ROTATION: SerialUSB.println("4 - Rotation"); break;
    case MODE_SAVE_EXIT: SerialUSB.println("5 - Save & Exit"); break;
    case MODE_EXIT_NO_SAVE: SerialUSB.println("6 - Exit Without Save"); break;
  }
  SerialUSB.println();
  SerialUSB.println("Press 1-6 to select mode, arrow keys to adjust.");
  SerialUSB.println("====================================");
}

void setMode(int mode) {
  if (mode >= 1 && mode <= 6) {
    currentMode = (CalibrationMode)(mode - 1);
    
    // Modes 5 and 6 are actions, not adjustment modes
    if (currentMode == MODE_SAVE_EXIT) {
      saveAndExit();
    } else if (currentMode == MODE_EXIT_NO_SAVE) {
      exitWithoutSaving();
    } else {
      // For adjustment modes (1-4), show the mode prompt
      showModePrompt();
    }
  }
}

bool checkUnsavedChanges() {
  if (!hasUnsavedChanges) {
    return false;
  }
  
  SerialUSB.println();
  SerialUSB.println("WARNING: You have unsaved changes!");
  SerialUSB.println("Press 'y' to continue without saving, ESC to cancel, or any other key to cancel.");
  
  while (!SerialUSB.available()) {
    delay(10);
  }
  
  char response = SerialUSB.read();
  while (SerialUSB.available()) SerialUSB.read(); // Clear buffer
  
  // ESC (27) or other keys cancel, only 'y' continues
  if (response == 27) {  // ESC key
    SerialUSB.println("Operation cancelled.");
    return false;
  }
  
  return (response == 'y' || response == 'Y');
}

void saveAndExit() {
  exportConfig();
  hasUnsavedChanges = false;
  hasEverSaved = true;
  lastSavedState.usableOriginX = usableOriginX;
  lastSavedState.usableOriginY = usableOriginY;
  lastSavedState.usableWidth = usableWidth;
  lastSavedState.usableHeight = usableHeight;
  lastSavedState.rotation = currentRotation;
  lastSavedState.frameThickness = frameThickness;
  
  SerialUSB.println();
  SerialUSB.println("Calibration saved. You can now close this tool.");
  SerialUSB.println("(Or press any key to continue calibrating)");
}

void exitWithoutSaving() {
  if (hasUnsavedChanges && !checkUnsavedChanges()) {
    SerialUSB.println("Exit cancelled. Returning to calibration.");
    currentMode = MODE_NONE;  // Reset to no mode
    return;
  }
  
  SerialUSB.println();
  SerialUSB.println("Exiting without saving. Goodbye!");
  SerialUSB.println("(Reset board or press reset button to restart)");
  while(true) {
    delay(1000); // Halt execution
  }
}

bool validateAndClampBounds() {
  // Comprehensive bounds validation to prevent off-screen drawing
  // Returns true if bounds were modified, false if already valid
  
  int maxX = tft.width();
  int maxY = tft.height();
  bool modified = false;
  
  // Clamp origin to valid range
  if (usableOriginX < 0) {
    usableOriginX = 0;
    modified = true;
  }
  if (usableOriginY < 0) {
    usableOriginY = 0;
    modified = true;
  }
  
  // Ensure origin doesn't exceed display bounds
  if (usableOriginX >= maxX) {
    usableOriginX = maxX - 1;
    modified = true;
  }
  if (usableOriginY >= maxY) {
    usableOriginY = maxY - 1;
    modified = true;
  }
  
  // Clamp width and height to prevent exceeding display bounds
  int maxAllowedWidth = maxX - usableOriginX;
  int maxAllowedHeight = maxY - usableOriginY;
  
  if (usableWidth > maxAllowedWidth) {
    usableWidth = maxAllowedWidth;
    modified = true;
  }
  if (usableHeight > maxAllowedHeight) {
    usableHeight = maxAllowedHeight;
    modified = true;
  }
  
  // Ensure minimum size
  if (usableWidth < 10) {
    usableWidth = 10;
    modified = true;
  }
  if (usableHeight < 10) {
    usableHeight = 10;
    modified = true;
  }
  
  // Final safety check: ensure total bounds don't exceed display
  if (usableOriginX + usableWidth > maxX) {
    usableWidth = maxX - usableOriginX;
    modified = true;
  }
  if (usableOriginY + usableHeight > maxY) {
    usableHeight = maxY - usableOriginY;
    modified = true;
  }
  
  if (modified) {
    SerialUSB.println("WARNING: Bounds clamped to valid range");
    SerialUSB.print("  Valid area: ");
    SerialUSB.print(usableOriginX);
    SerialUSB.print(",");
    SerialUSB.print(usableOriginY);
    SerialUSB.print(" ");
    SerialUSB.print(usableWidth);
    SerialUSB.print("x");
    SerialUSB.println(usableHeight);
  }
  
  return modified;
}

void redrawFrame() {
  // Validate bounds before drawing
  validateAndClampBounds();
  clearScreen();
  drawFrame();
}

void adjustEdge(char direction) {
  // Guard: need initial bounds set
  if (usableWidth == 0 || usableHeight == 0) {
    SerialUSB.println("ERROR: Set initial bounds first using 'bounds L,R,T,B' or 'frame' command");
    return;
  }
  
  int step = 1;
  bool changed = false;
  int maxX = tft.width();
  int maxY = tft.height();
  
  switch(direction) {
    case 'U': // Up arrow - decrease top edge (expand upward)
      if (usableOriginY > 0 && (usableOriginY - step) >= 0) {
        usableOriginY -= step;
        usableHeight += step;
        // Ensure total doesn't exceed display height
        if (usableOriginY + usableHeight > maxY) {
          usableHeight = maxY - usableOriginY;
        }
        changed = true;
      }
      break;
    case 'D': // Down arrow - increase top edge (contract from top)
      if (usableHeight > 10 && usableOriginY + step < maxY) {
        usableOriginY += step;
        usableHeight -= step;
        changed = true;
      }
      break;
    case 'L': // Left arrow - decrease left edge (expand leftward)
      if (usableOriginX > 0 && (usableOriginX - step) >= 0) {
        usableOriginX -= step;
        usableWidth += step;
        // Ensure total doesn't exceed display width
        if (usableOriginX + usableWidth > maxX) {
          usableWidth = maxX - usableOriginX;
        }
        changed = true;
      }
      break;
    case 'R': // Right arrow - increase left edge (contract from left)
      if (usableWidth > 10 && usableOriginX + step < maxX) {
        usableOriginX += step;
        usableWidth -= step;
        changed = true;
      }
      break;
  }
  
  if (changed) {
    // Validate and clamp bounds after adjustment
    validateAndClampBounds();
    markModified();
    redrawFrame();
    SerialUSB.print("Edge adjusted. Usable: ");
    SerialUSB.print(usableOriginX);
    SerialUSB.print(",");
    SerialUSB.print(usableOriginY);
    SerialUSB.print(" ");
    SerialUSB.print(usableWidth);
    SerialUSB.print("x");
    SerialUSB.println(usableHeight);
  }
}

void moveFrame(char direction) {
  // Guard: need initial bounds set
  if (usableWidth == 0 || usableHeight == 0) {
    SerialUSB.println("ERROR: Set initial bounds first using 'bounds L,R,T,B' or 'frame' command");
    return;
  }
  
  int step = 1;
  int maxX = tft.width();
  int maxY = tft.height();
  bool changed = false;
  
  switch(direction) {
    case 'U': // Up
      if (usableOriginY > 0) {
        usableOriginY -= step;
        changed = true;
      }
      break;
    case 'D': // Down
      if (usableOriginY + usableHeight < maxY) {
        usableOriginY += step;
        changed = true;
      }
      break;
    case 'L': // Left
      if (usableOriginX > 0) {
        usableOriginX -= step;
        changed = true;
      }
      break;
    case 'R': // Right
      if (usableOriginX + usableWidth < maxX) {
        usableOriginX += step;
        changed = true;
      }
      break;
  }
  
  if (changed) {
    // Validate and clamp bounds after move
    validateAndClampBounds();
    markModified();
    redrawFrame();
    SerialUSB.print("Frame moved. Origin: (");
    SerialUSB.print(usableOriginX);
    SerialUSB.print(",");
    SerialUSB.print(usableOriginY);
    SerialUSB.println(")");
  }
}

void adjustThickness(char direction) {
  bool changed = false;
  
  if (direction == 'U' && frameThickness < 5) {
    frameThickness++;
    changed = true;
  } else if (direction == 'D' && frameThickness > 1) {
    frameThickness--;
    changed = true;
  }
  
  if (changed) {
    markModified();
    redrawFrame();
    SerialUSB.print("Thickness: ");
    SerialUSB.println(frameThickness);
  }
}

void rotateDisplay(char direction) {
  if (direction == 'L') {
    currentRotation = (currentRotation + 3) % 4; // CCW
  } else if (direction == 'R') {
    currentRotation = (currentRotation + 1) % 4; // CW
  }
  
  setRotation(currentRotation);
  markModified();
  SerialUSB.print("Rotation: ");
  SerialUSB.println(currentRotation);
}

void handleArrowKey(char key) {
  switch(currentMode) {
    case MODE_EDGE_ADJUST:
      adjustEdge(key);
      break;
    case MODE_FRAME_MOVE:
      moveFrame(key);
      break;
    case MODE_THICKNESS:
      adjustThickness(key);
      break;
    case MODE_ROTATION:
      rotateDisplay(key);
      break;
    case MODE_SAVE_EXIT:
    case MODE_EXIT_NO_SAVE:
      // These modes don't use arrow keys - ignore
      SerialUSB.println("Arrow keys not used in this mode. Press 1-4 to select adjustment mode.");
      break;
  }
}

// ==================== End Arrow Key Functions ====================

// ==================== Display Selection and Config Creation ====================

String readSerialLine() {
  // Read a line from serial, waiting for newline
  String input = "";
  while (true) {
    while (!SerialUSB.available()) {
      delay(10);
    }
    char c = SerialUSB.read();
    if (c == '\n' || c == '\r') {
      if (input.length() > 0) {
        return input;
      }
    } else if (c >= 32 && c <= 126) {  // Printable characters
      input += c;
      SerialUSB.print(c);  // Echo
    } else if (c == 8 || c == 127) {  // Backspace
      if (input.length() > 0) {
        input.remove(input.length() - 1);
        SerialUSB.print("\b \b");  // Erase character
      }
    }
  }
}

void createNewDisplayConfig() {
  SerialUSB.println();
  SerialUSB.println("========== CREATE NEW DISPLAY CONFIG ==========");
  SerialUSB.println();
  SerialUSB.println("This will guide you through creating a new .config file.");
  SerialUSB.println("After calibration, you'll need to copy the generated config");
  SerialUSB.println("to a file named <DisplayName>.config in the project root.");
  SerialUSB.println();
  
  // Get display name
  SerialUSB.print("Enter display name (e.g., DueLCD03): ");
  currentDisplayName = readSerialLine();
  SerialUSB.println();
  
  if (currentDisplayName.length() == 0) {
    SerialUSB.println("ERROR: Display name cannot be empty!");
    SerialUSB.println("Calibration tool cannot proceed without a display name.");
    SerialUSB.println("Please reset and try again.");
    while(true) { delay(1000); }  // Halt
  }
  
  SerialUSB.println("Display name set to: " + currentDisplayName);
  SerialUSB.println();
  SerialUSB.println("Note: Initial bounds will be set from published dimensions.");
  SerialUSB.println("      Use calibration modes to fine-tune the display edges.");
  SerialUSB.println();
  
  configExists = true;  // Mark as ready to calibrate
}

void selectOrCreateDisplay() {
  SerialUSB.println("========== DISPLAY SELECTION ==========");
  SerialUSB.println();
  SerialUSB.println("IMPORTANT: This calibration tool requires a display configuration.");
  SerialUSB.println();
  SerialUSB.println("Since this tool runs on the Arduino Due, it cannot read .config");
  SerialUSB.println("files from your computer. You must specify which display you are");
  SerialUSB.println("calibrating.");
  SerialUSB.println();
  SerialUSB.println("Available options:");
  SerialUSB.println("  1. Calibrate existing display (enter name manually)");
  SerialUSB.println("  2. Create new display configuration");
  SerialUSB.println("  3. Exit calibration tool");
  SerialUSB.println();
  SerialUSB.print("Select option (1-3): ");
  
  String choice = readSerialLine();
  SerialUSB.println();
  
  if (choice == "1") {
    SerialUSB.print("Enter display name to calibrate (e.g., DueLCD01): ");
    currentDisplayName = readSerialLine();
    SerialUSB.println();
    
    if (currentDisplayName.length() == 0) {
      SerialUSB.println("ERROR: Display name cannot be empty!");
      SerialUSB.println("Calibration tool cannot proceed. Please reset and try again.");
      while(true) { delay(1000); }  // Halt
    }
    
    SerialUSB.println("Calibrating display: " + currentDisplayName);
    SerialUSB.println("Note: Ensure " + currentDisplayName + ".config exists on your computer");
    SerialUSB.println("      or create it after calibration using the exported data.");
    configExists = true;
    
  } else if (choice == "2") {
    createNewDisplayConfig();
    
  } else if (choice == "3") {
    SerialUSB.println("Exiting calibration tool.");
    SerialUSB.println("Please reset the Arduino to restart.");
    while(true) { delay(1000); }  // Halt
    
  } else {
    SerialUSB.println("Invalid choice. Please reset and select 1, 2, or 3.");
    while(true) { delay(1000); }  // Halt
  }
  
  SerialUSB.println();
  SerialUSB.println("======================================");
}

// ==================== End Display Selection ====================

void loop() {
  if (SerialUSB.available()) {
    char c = SerialUSB.read();
    
    // Check for Ctrl-C (ASCII 3)
    if (c == 3) {
      SerialUSB.println();
      SerialUSB.println("Ctrl-C detected - initiating save & exit sequence...");
      saveAndExit();
      return;
    }
    
    // Check for ESC key or ANSI escape sequence (arrow keys)
    if (c == 27) { // ESC character
      delay(10); // Wait to see if it's an arrow key sequence
      if (SerialUSB.available() && SerialUSB.peek() == '[') {
        // This is an arrow key sequence
        SerialUSB.read(); // consume '['
        if (SerialUSB.available()) {
          char arrowCode = SerialUSB.read();
          char direction;
          
          switch(arrowCode) {
            case 'A': direction = 'U'; break; // Up
            case 'B': direction = 'D'; break; // Down
            case 'C': direction = 'R'; break; // Right
            case 'D': direction = 'L'; break; // Left
            default: return; // Unknown sequence
          }
          
          handleArrowKey(direction);
        }
      } else {
        // Plain ESC key (not followed by '[')
        handleEscapeKey();
      }
      return;
    }
    // Check for mode selection (1-6)
    else if (c >= '1' && c <= '6') {
      setMode(c - '0');
    }
    // Check for newline (legacy command mode)
    else if (c == '\n' || c == '\r') {
      // Ignore empty newlines
    }
    // Handle text commands (for backwards compatibility)
    else {
      String command = String(c) + SerialUSB.readStringUntil('\n');
      processCommand(command);
    }
  }
  
  delay(10);  // Small delay
}