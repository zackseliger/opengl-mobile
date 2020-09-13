#include "game.h"
#include "image.h"
#include <fstream>
#include <string>
#include <png.h>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <freetype-gl.h>

#ifdef __ANDROID__
#include <android/log.h>
#define  ALOG(...)  __android_log_print(ANDROID_LOG_INFO,"io.deemo",__VA_ARGS__)
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <android/asset_manager.h>
#elif __APPLE__
#include <OpenGLES/ES2/gl.h>
#endif

// stuff that should be in the engine
double screenWidth;
double screenHeight;
// programs
GLuint colorProgram;
GLint c_uProjLoc;
GLint c_uCamLoc;
GLint c_posLoc;
GLint c_color;

GLuint textureProgram;
GLint p_uProjLoc;
GLint p_uCamLoc;
GLint p_posLoc;
GLint p_texLoc;
GLint p_texCoord;
GLuint p_color;

// stuff that's just there
float topX = 0;
float topY = -100;
GLint textureId;

// freetype stuff
FT_Library library;
FT_Face face;
ftgl::texture_atlas_t* texAtlas;
ftgl::texture_font_t* texFont;

// utilities
void LOG(const char* message) {
  #ifdef __ANDROID__
  ALOG("%s", message);
  #elif __APPLE__
  printf("%s\n", message);
  #endif
}
#ifdef __ANDROID__
AAssetManager* assetManager;
FileData getAsset(const char* filename) {
  AAsset* asset = AAssetManager_open(assetManager, filename, AASSET_MODE_STREAMING);
  assert(asset != nullptr);

  // read file, manually adding EOF
  long size = AAsset_getLength(asset);
  void* buffer = malloc(size+1);
  AAsset_read(asset, buffer, size);
  ((char*)buffer)[size] = 0;

  return (FileData){size+1, buffer, asset};
}
void freeAsset(FileData asset) {
    AAsset_close((AAsset*)asset.handle);
    free((void*)asset.data);
}
void initAssetManager(AAssetManager* manager) {
  assetManager = manager;
}
// Apple implementation is in AssetsUtils.mm
#endif

GLuint buildShader(GLenum type, const GLchar* shaderSrc) {
  GLuint shader;
  GLint compiled;

  // Create the shader object
  shader = glCreateShader(type);
  // Load the shader source
  glShaderSource(shader, 1, &shaderSrc, nullptr);
  // Compile the shader
  glCompileShader(shader);
  // Check the compile status
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);

  // check how our shader compiled
  if(compiled == GL_FALSE) {
    GLint maxLength = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);

    // The maxLength includes the NULL character
    const char* errorLog = (const char*)malloc(maxLength);
    glGetShaderInfoLog(shader, maxLength, &maxLength, (GLchar*)errorLog);
    LOG("COULDN'T COMPILE SHADER:");
    LOG(errorLog);
    free((void*)errorLog);

    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

GLuint buildProgram(const char* vertexShaderPath, const char* fragmentShaderPath) {
  // load files
  FileData vShaderFile = getAsset(vertexShaderPath);
  FileData fShaderFile = getAsset(fragmentShaderPath);
    
  // create shaders
  GLuint vShader = buildShader(GL_VERTEX_SHADER, (const char*)vShaderFile.data);
  GLuint fShader = buildShader(GL_FRAGMENT_SHADER, (const char*)fShaderFile.data);
    
  // create and return program
  GLuint programId = glCreateProgram();
  glAttachShader(programId, vShader);
  glAttachShader(programId, fShader);
  glLinkProgram(programId);
    
  // free assets
  freeAsset(vShaderFile);
  freeAsset(fShaderFile);

  // make sure it worked out
  GLint isLinked = 0;
  glGetProgramiv(programId, GL_LINK_STATUS, &isLinked);
  if (isLinked == GL_FALSE) {
    GLint maxLength = 0;
    glGetProgramiv(programId, GL_INFO_LOG_LENGTH, &maxLength);

    // The maxLength includes the NULL character
    const char* errorLog = (const char*)malloc(maxLength);
    glGetProgramInfoLog(programId, maxLength, &maxLength, (GLchar*)errorLog);
    LOG("COULDN'T LINK PROGRAM:");
    LOG(errorLog);
    free((void*)errorLog);

    // delete stuff and return 0
    glDeleteProgram(programId);
    glDeleteShader(vShader);
    glDeleteShader(fShader);
    return 0;
  }

  return programId;
}

void glSetup(double width, double height) {
  // color program
  colorProgram = buildProgram("shaders/color.vsh", "shaders/color.fsh");
  c_posLoc = glGetAttribLocation(colorProgram, "vPosition");
  c_uProjLoc = glGetUniformLocation(colorProgram, "uProj");
  c_uCamLoc = glGetUniformLocation(colorProgram, "uCam");
  c_color = glGetAttribLocation(colorProgram, "aColor");

  // texture program
  textureProgram = buildProgram("shaders/texture.vsh", "shaders/texture.fsh");
  p_posLoc = glGetAttribLocation(textureProgram, "vPosition");
  p_uProjLoc = glGetUniformLocation(textureProgram, "uProj");
  p_uCamLoc = glGetUniformLocation(textureProgram, "uCam");
  p_texLoc = glGetUniformLocation(textureProgram, "uTextureUnit");
  p_texCoord = glGetAttribLocation(textureProgram, "aTextureCoordinates");
  p_color = glGetUniformLocation(textureProgram, "uColor");

  // enable transparency
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // setup initial matrix values
  handleResize(width, height);
  GLfloat initCamMat[] = {1, 0, 0, 0,
                          0, 1, 0, 0,
                          0, 0, 1, 0,
                          0, 0, 0, 1};
  glUseProgram(textureProgram);
  glUniformMatrix4fv(p_uCamLoc, 1, GL_FALSE, initCamMat);
  glUseProgram(colorProgram);
  glUniformMatrix4fv(c_uCamLoc, 1, GL_FALSE, initCamMat);

  // setup image
  RawImageData imageData = getImage((const char*)"image/pngtest.png");
  textureId = loadTexture(imageData.width, imageData.height, imageData.gl_color_format, imageData.data);
  releaseImage(&imageData);

  //setup freetype-gl
  FileData font = getAsset("Poetsen.ttf");//NOTE: THIS ISN'T FREED BC WE NEED TO ADD TO TEX ATLAS IN REALTIME!!
  texAtlas = ftgl::texture_atlas_new(512, 512, 1);
  texFont = ftgl::texture_font_new_from_memory(texAtlas, 32, font.data, font.size);
//  freeAsset(font);
}

void drawText(const char* text, float r, float g, float b) {
  GLfloat verticies[24*strlen(text)]; // 24 floats per char
  float currx = 0.0; // text goes to the right
  
  // add in each character to verticies
  bool reload = false;
  for (int i = 0; i < strlen(text); i++) {
    // if glyph isn't already loaded, we need to update our font texture atlas
    if (!texture_font_find_glyph(texFont, (const char*)&text[i])) {
      reload = true;
    }
    
    // get glyph
    const char test[2] = {text[i]};
    ftgl::texture_glyph_t* glyph = ftgl::texture_font_get_glyph(texFont, test);
    
    // set up varticies
    float x0 = currx + -150.0f + glyph->offset_x;
    float y0 = -300.0f - glyph->offset_y;
    float x1 = x0 + glyph->width;
    float y1 = y0 + glyph->height;
    float u0 = glyph->s0;
    float v0 = glyph->t0;
    float u1 = glyph->s1;
    float v1 = glyph->t1;
    
    verticies[i*24 + 0] = x0;
    verticies[i*24 + 1] = y0;
    verticies[i*24 + 2] = u0;
    verticies[i*24 + 3] = v0;
    
    verticies[i*24 + 4] = x0;
    verticies[i*24 + 5] = y1;
    verticies[i*24 + 6] = u0;
    verticies[i*24 + 7] = v1;
    
    verticies[i*24 + 8] = x1;
    verticies[i*24 + 9] = y0;
    verticies[i*24 + 10] = u1;
    verticies[i*24 + 11] = v0;
    
    verticies[i*24 + 12] = x1;
    verticies[i*24 + 13] = y1;
    verticies[i*24 + 14] = u1;
    verticies[i*24 + 15] = v1;
    
    verticies[i*24 + 16] = x0;
    verticies[i*24 + 17] = y1;
    verticies[i*24 + 18] = u0;
    verticies[i*24 + 19] = v1;
    
    verticies[i*24 + 20] = x1;
    verticies[i*24 + 21] = y0;
    verticies[i*24 + 22] = u1;
    verticies[i*24 + 23] = v0;
    
    currx += glyph->advance_x;
  }
  
  if (reload) {
    texAtlas->id = loadTexture(texAtlas->width, texAtlas->height, GL_ALPHA, texAtlas->data);
  }
  
  // render verticies
  GLuint bufferLoc;
  glGenBuffers(1, &bufferLoc);
  glBindBuffer(GL_ARRAY_BUFFER, bufferLoc);
  glBufferData(GL_ARRAY_BUFFER, sizeof(verticies), verticies, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  
  glUseProgram(textureProgram);
  glUniform4f(p_color, r, g, b, 0.0); // color
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texAtlas->id);
  glUniform1i(p_texLoc, 0);
  
  glBindBuffer(GL_ARRAY_BUFFER, bufferLoc);
  glEnableVertexAttribArray(p_posLoc);
  glEnableVertexAttribArray(p_texCoord);
  glVertexAttribPointer(p_posLoc, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), 0);
  glVertexAttribPointer(p_texCoord, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (const void*)8);
  
  glDrawArrays(GL_TRIANGLES, 0, strlen(text)*6);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  
  glDisableVertexAttribArray(p_posLoc);
  glDisableVertexAttribArray(p_texCoord);
  glUniform4f(p_color, 0.0, 0.0, 0.0, 0.0);// reset color
}

void glRender() {
  glClearColor(0.2, 0.1, 0.9, 1.0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // draw texture
  GLfloat positions[] = {-100.0f, -200.0f, -100.0f, 0.0f, 100.0f, -200.0f, 100.0f, 0.0f};
  GLfloat uvCoords[] = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f};

  glUseProgram(textureProgram);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texAtlas->id); // textureId
  glUniform1i(p_texLoc, 0);
  glVertexAttribPointer(p_posLoc, 2, GL_FLOAT, GL_FALSE, 0,  positions);
  glVertexAttribPointer(p_texCoord, 2, GL_FLOAT, GL_FALSE, 0, uvCoords);
  glEnableVertexAttribArray(p_posLoc);
  glEnableVertexAttribArray(p_texCoord);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glDisableVertexAttribArray(p_posLoc);
  glDisableVertexAttribArray(p_texCoord);

  // draw triangle
  GLfloat verticies[] = {topX,topY, -100,100, 100,100, 200,200};
  GLfloat colors[] = {1.0,0.0,0.0, 0.5,0.5,0.5, 0.5,0.5,0.5, 0.6,0.6,0.3};
  glUseProgram(colorProgram);
  glVertexAttribPointer(c_posLoc, 2, GL_FLOAT, GL_FALSE, 0, verticies);
  glVertexAttribPointer(c_color, 3, GL_FLOAT, GL_FALSE, 0, colors);
  glEnableVertexAttribArray(c_posLoc);
  glEnableVertexAttribArray(c_color);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDisableVertexAttribArray(c_posLoc);
  glDisableVertexAttribArray(c_color);
  
  // draw texture 2
  GLfloat verticies2[] = {
    -100.0f, 100.0f, 0.0f, 0.0f,//left, top, bottom-left
    -100.0f, 300.0f, 0.0f, 1.0f,//left, bottom, top-left
     100.0f, 100.0f, 1.0f, 0.0f,//right, top, bottom-right
    
     100.0f, 300.0f, 1.0f, 1.0f,//right, bottom, top-right
    -100.0f, 300.0f, 0.0f, 1.0f,//left, bottom, top-left
     100.0f, 100.0f, 1.0f, 0.0f//right, top, bottom-right
  };

  GLuint bufferLoc;
  glGenBuffers(1, &bufferLoc);
  glBindBuffer(GL_ARRAY_BUFFER, bufferLoc);
  glBufferData(GL_ARRAY_BUFFER, sizeof(verticies2), verticies2, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glUseProgram(textureProgram);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, textureId);
  glUniform1i(p_texLoc, 0);
  glBindBuffer(GL_ARRAY_BUFFER, bufferLoc);
  glEnableVertexAttribArray(p_posLoc);
  glEnableVertexAttribArray(p_texCoord);
  glVertexAttribPointer(p_posLoc, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), 0);
  glVertexAttribPointer(p_texCoord, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (const void*)8);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glDisableVertexAttribArray(p_posLoc);
  glDisableVertexAttribArray(p_texCoord);
  
  // draw text
  drawText("Hello world. I am here!", 0.8, 0.2, 0.2);
}

// input events
void startTouch(int id, float x, float y) {
  topX = x - screenWidth/2;
  topY = y - screenHeight/2;
}
void moveTouch(int id, float x, float y) {
  topX = x - screenWidth/2;
  topY = y - screenHeight/2;
}
void endTouch(int id, float x, float y) {
  topX = x - screenWidth/2;
  topY = y - screenHeight/2;
}

// misc events
void handleResize(double width, double height) {
  screenWidth = width;
  screenHeight = height;
  glViewport(0, 0, screenWidth, screenHeight);

  // redo our projection matrix
  float right = screenWidth / 2;
  float left = -screenWidth / 2;
  float top = -screenHeight / 2;
  float bot = screenHeight / 2;
  GLfloat projMat[] = {
          static_cast<GLfloat>(2.0 / (right - left)), 0, 0, -(right + left) / (right - left),
          0, static_cast<GLfloat>(2.0 / (top - bot)), 0, -(top + bot) / (top - bot),
          0, 0, -2.0 / (1.0 + 1.0), -(1.0 - 1.0) / (1.0 + 1.0),
          0, 0, 0, 1
  };
  glUseProgram(textureProgram);
  glUniformMatrix4fv(p_uProjLoc, 1, GL_FALSE, projMat);
  glUseProgram(colorProgram);
  glUniformMatrix4fv(c_uProjLoc, 1, GL_FALSE, projMat);
}
