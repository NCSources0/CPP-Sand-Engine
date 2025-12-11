#include "SDL3/SDL.h"
#include "SDL3_ttf/SDL_ttf.h"
#include <string>
#include <cstring>

// Window config
#define TITLE   "C++ Sand Sim" // Window title
#define WIDTH   1280           // Window width
#define HEIGHT  720            // Window height
#define PX_SIZE 4              // Pixel size
#define FLAGS   0              // Window flags
#define DRIVER  "opengl"       // Renderer driver - NULL "direct3d" "direct3d11" "direct3d12" "opengl" "opengles2" "opengles" "metal" "vulkan" "gpu" "software"

#define WIN_WIDTH  (WIDTH / PX_SIZE)  // Pixel width
#define WIN_HEIGHT (HEIGHT / PX_SIZE) // Pixel height

// Font config
#define BASEPATH    "fonts/Nunito/"                      // Base font path
#define FONT_SIZE   16                                   // Font size
#define MIN_WGT     200                                  // Minimum font weight
#define MAX_WGT     900                                  // Maximum font weight
#define WGT_STEP    100                                  // Font weight step
#define WGTS        ((MAX_WGT - MIN_WGT) / WGT_STEP + 1) // Number of weight variations
#define TXT_RESERVE 256                                  // Text buffer reserve size

// Textbox config
#define BOX_C SDL_Color{0, 0, 0, 127} // Textbox color
#define BOX_W 128                     // Textbox width
#define BOX_X 8                       // Textbox X position
#define BOX_Y 8                       // Textbox Y position
#define PAD_X 4                       // Textbox padding left/right
#define PAD_Y 4                       // Textbox padding top/bottom

// Brushes
#define VERTEXES    32 // Number of brush vertexes
#define BRUSH_PAINT 0  // Hard brush
#define BRUSH_SPRAY 1  // Soft brush

using namespace std;

template <typename T, size_t N>
constexpr size_t lengthOf(const T (&)[N]) {
  return N;
}

struct Box2D {
  float x;
  float y;
  float w;
  float h;
};

struct coords {
  int x;
  int y;
};

struct effect {
  string type;
  int params[8];
};

struct Material {
  string name;
  SDL_Color color;
  coords rules[8][2]; // {{check}, {dx, dy}}(8 max)
  effect effects[16]; // {type, params... (8 max)}(16 max)
};

SDL_Window *window = nullptr;              // Main window
SDL_Renderer *renderer = nullptr;          // Main renderer
TTF_TextEngine *rendererEngine = nullptr;  // Renderer text engine
TTF_TextEngine *surfaceEngine = nullptr;   // Surface text engine
TTF_Font *fonts[WGTS * 2 + 1] = {nullptr}; // Font array
SDL_DisplayID displayID = 0;               // Display ID
const SDL_DisplayMode *display = nullptr;  // Display properties ID

int grid[WIN_WIDTH][WIN_HEIGHT] = {0};       // Simulation grid
int bufferGrid[WIN_WIDTH][WIN_HEIGHT] = {0}; // Buffer grid
int material = 1;                            // Current material
bool keys[SDL_SCANCODE_COUNT] = {false};     // Current key pressed
bool leftMouse = false;                      // Left mouse button state
bool rightMouse = false;                     // Right mouse button state
bool middleMouse = false;                    // Middle mouse button state
coords scroll = {0, 0};                      // Mouse scroll
coords mouse = {0, 0};                       // Mouse position
SDL_Time lastTick = 1;                       // Last tick time (ns)
float deltaTime = 1;                         // Delta time (ms)
float fps;                                   // Frames per second
string matKeys[11] = {"`", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0"}; // Material selection keys
int matKeysLength = lengthOf(matKeys);    // Number of material selection keys
float brushSize = 16;                      // Brush size

string text = ""; // Text to display
int lines = 0;    // Number of text lines

Material materials[4] = {
  {
    "Eraser",
    { 0, 0, 0 }
  },
  {
    "Wall",
    { 127, 127, 127 }
  },
  {
    "Sand",
    { 255, 255, 200 },
    { {{0, 1}, {0, 1}}, {{-1, 0}, {-1, 1}}, {{1, 0}, {1, 1}} }
  }
};
int materialsLength = lengthOf(materials);

void addTextLine(string txt) {
  text += txt + "\n";
  lines++;
}

void addText(string txt) {
  text += txt;
}

void renderText(float x, float y, int weight = 400, bool italic = false) {
  weight = (SDL_clamp(weight, MIN_WGT, MAX_WGT) - MIN_WGT) / WGT_STEP;
  TTF_Text *thisText = TTF_CreateText(rendererEngine, fonts[weight + italic * WGTS], text.c_str(), 0);
  TTF_DrawRendererText(thisText, x, y);

  TTF_DestroyText(thisText);
}

void clearText() {
  text = "";
  text.reserve(TXT_RESERVE);
  lines = 0;
}

TTF_Font* createFont(string path, float ptSize = FONT_SIZE, int lineskip = FONT_SIZE, TTF_Font *fallback = nullptr) {
  TTF_Font *font = TTF_OpenFont(path.c_str(), ptSize);
  TTF_AddFallbackFont(font, fallback);
  TTF_SetFontLineSkip(font, lineskip);
  return font;
}

void createFonts(float ptSize) {
  fonts[WGTS * 2] = TTF_OpenFont("fonts/NotoColorEmoji.ttf", ptSize);
  for (int i = 0; i < WGTS; i++) {
    string base = BASEPATH + to_string(i * WGT_STEP + MIN_WGT);
    fonts[i] =        createFont((base + ".ttf").c_str(),  ptSize, ptSize, fonts[WGTS * 2]);
    fonts[i + WGTS] = createFont((base + "I.ttf").c_str(), ptSize, ptSize, fonts[WGTS * 2]);
  }
}

void closeFonts() {
  for (int i = 0; i < WGTS * 2 + 1; i++) {
    if (fonts[i] != nullptr) TTF_CloseFont(fonts[i]);
    fonts[i] = nullptr;
  }
}

void cleanup() {
  closeFonts();
  TTF_DestroyRendererTextEngine(rendererEngine);
  TTF_DestroySurfaceTextEngine(surfaceEngine);
  rendererEngine = nullptr;
  surfaceEngine = nullptr;
  TTF_Quit();

  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  renderer = nullptr;
  window = nullptr;
  SDL_Quit();
}

void px(float x, float y, int r, int g, int b) {
  SDL_SetRenderDrawColor(renderer, r, g, b, 255);
  SDL_FRect rect = { x * PX_SIZE, y * PX_SIZE, PX_SIZE, PX_SIZE };
  SDL_RenderFillRect(renderer, &rect);
}

bool key(string keycode) {
  SDL_Scancode scancode = SDL_GetScancodeFromName(keycode.c_str());
  if (scancode == SDL_SCANCODE_UNKNOWN) return false;
  return keys[scancode];
}

bool inBounds(float x, float y) {
  return (x >= 0 && y >= 0 && x < WIN_WIDTH && y < WIN_HEIGHT);
}

int getID(float x, float y) {
  return y * (WIN_WIDTH) + x;
}

float points2pixels(float points) {
  return points * (4.0f * SDL_GetWindowDisplayScale(window) / 3.0f);
}

bool isLiquid(int i) {
  if (i <= 0 || i >= materialsLength) return false;
  return materials[i].effects[0].type == "Liquid";
}

void initBuffer() {
  memcpy(bufferGrid, grid, sizeof(bufferGrid));
}

void commitBuffer() {
  memcpy(grid, bufferGrid, sizeof(grid));
}

bool canMove(coords rule, int x, int y) {
  int fx = x + rule.x;
  int fy = y + rule.y;
  if (!inBounds(fx, fy)) return false;
  return grid[fx][fy] == 0;
}

bool move(coords rule, int x, int y) {
  int fx = x + rule.x;
  int fy = y + rule.y;

  if (!canMove(rule, x, y)) return false;
  
  bufferGrid[fx][fy] = grid[x][y];
  bufferGrid[x][y] = grid[fx][fy];

  return true;
}

void render() {
  for (int i = 0; i < SDL_min(matKeysLength, materialsLength); i++) {
    if (key(matKeys[i])) material = i;
  }

  initBuffer();
  for (int y = 0; y < WIN_HEIGHT; y++) {
    for (int x = 0; x < WIN_WIDTH; x++) {
      int id = grid[x][y];
      if (id <= 0) continue;

      Material &mat = materials[id];
      for (int j = 0; j < lengthOf(mat.rules); j++) {
        if (canMove(mat.rules[j][0], x, y)) if (move(mat.rules[j][1], x, y)) break;
      }
    }
  }

  brushSize += scroll.y;
  brushSize = SDL_clamp(brushSize, 1.0f, 64.0f);

  coords m = { mouse.x / PX_SIZE, mouse.y / PX_SIZE };
  if (leftMouse && inBounds(m.x, m.y)) {
    int half = brushSize / 2;
    int end = half - SDL_fmodf(brushSize, 1);
    for (int x = -half; x <= end; x++) {
      for (int y = -half; y <= end; y++) {
        int dx = m.x + x;
        int dy = m.y + y;
        if (!inBounds(dx, dy)) continue;

        float dist = SDL_sqrtf((float)(x * x + y * y));
        if (dist > brushSize / 2) continue;

        bufferGrid[dx][dy] = material;
      }
    }
    bufferGrid[m.x][m.y] = material;
  }

  commitBuffer();
  for (int x = 0; x < WIN_WIDTH; x++) {
    for (int y = 0; y < WIN_HEIGHT; y++) {
      Material &mat = materials[grid[x][y]];
      px(x, y, mat.color.r, mat.color.g, mat.color.b);
    }
  }

  SDL_FPoint points[VERTEXES] = {NULL};
  float xp = m.x;
  float yp = m.y;
  for (int i = 0; i < VERTEXES; i++) {
    float angle = (float)i / (VERTEXES - 1.0f) * 2.0f * 3.14159265f;
    float bx = xp + SDL_cosf(angle) * (brushSize / 2.0f);
    float by = yp + SDL_sinf(angle) * (brushSize / 2.0f);
    points[i] = SDL_FPoint{ bx * PX_SIZE, by * PX_SIZE };
  }

  SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
  SDL_RenderLines(renderer, points, VERTEXES);

  clearText();
  addTextLine("C++ Sand Sim\n");
  addTextLine("Material: " + materials[material].name);
  addTextLine("FPS: " + to_string((int)fps));

  Box2D textBox = { BOX_X, BOX_Y, BOX_W + PAD_X * 2, points2pixels(lines * FONT_SIZE) + PAD_Y * 2 };

  SDL_FRect box = { textBox.x, textBox.y, textBox.w, textBox.h };
  SDL_SetRenderDrawColor(renderer, BOX_C.r, BOX_C.g, BOX_C.b, BOX_C.a);
  SDL_RenderFillRect(renderer, &box);

  renderText(textBox.x + PAD_X, textBox.y + PAD_Y);
}

int main(int argc, char* argv[]) {
  SDL_Init(SDL_INIT_VIDEO);
  TTF_Init();

  displayID = SDL_GetPrimaryDisplay();
  display = SDL_GetDesktopDisplayMode(displayID);

  window = SDL_CreateWindow(TITLE, WIDTH, HEIGHT, 0);
  renderer = SDL_CreateRenderer(window, "opengl");
  rendererEngine = TTF_CreateRendererTextEngine(renderer);
  surfaceEngine = TTF_CreateSurfaceTextEngine();

  createFonts(FONT_SIZE);

  bool running = true;
  SDL_Event event;
  while (running) {
    deltaTime = (SDL_GetTicksNS() - lastTick) / 1000000.0f;
    lastTick = SDL_GetTicksNS();
    fps = 1000.0f / deltaTime;

    while (SDL_PollEvent(&event)) {
      switch (event.type) {
        case SDL_EVENT_QUIT:
          running = false;
          break;
        
        case SDL_EVENT_KEY_DOWN:
          keys[event.key.scancode] = true;
          break;

        case SDL_EVENT_KEY_UP:
          keys[event.key.scancode] = false;
          break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
          if (event.button.button == SDL_BUTTON_LEFT) leftMouse = true;
          if (event.button.button == SDL_BUTTON_RIGHT) rightMouse = true;
          if (event.button.button == SDL_BUTTON_MIDDLE) middleMouse = true;
          break;

        case SDL_EVENT_MOUSE_BUTTON_UP:
          if (event.button.button == SDL_BUTTON_LEFT) leftMouse = false;
          if (event.button.button == SDL_BUTTON_RIGHT) rightMouse = false;
          if (event.button.button == SDL_BUTTON_MIDDLE) middleMouse = false;
          break;

        case SDL_EVENT_MOUSE_MOTION:
          mouse.x = event.motion.x;
          mouse.y = event.motion.y;
          scroll.x = 0;
          scroll.y = 0;
          break;

        case SDL_EVENT_MOUSE_WHEEL:
          scroll.x = event.wheel.x;
          scroll.y = event.wheel.y;
          if (SDL_fabsf(scroll.x) < 0.05) scroll.x = 0;
          if (SDL_fabsf(scroll.y) < 0.05) scroll.y = 0;
          break;

        default:
          break;
      }
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    render();
    SDL_RenderPresent(renderer);
  }

  cleanup();
  return 0;
}