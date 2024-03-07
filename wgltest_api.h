/*
 - name
 - GetProcAddress/wglGetProcAddress
 - GL api/winapi/driver api
 - default lib (opengl32.dll/gdi32.dll)
*/

GL_API_LOAD(glClearColor,      FUNC_GL, LIB_GL32)
GL_API_LOAD(glClear,           FUNC_GL, LIB_GL32)
GL_API_LOAD(glPushMatrix,      FUNC_GL, LIB_GL32)
GL_API_LOAD(glPopMatrix,       FUNC_GL, LIB_GL32)
GL_API_LOAD(glBegin,           FUNC_GL, LIB_GL32)
GL_API_LOAD(glEnd,             FUNC_GL, LIB_GL32)
GL_API_LOAD(glRotatef,         FUNC_GL, LIB_GL32)
GL_API_LOAD(glColor3f,         FUNC_GL, LIB_GL32)
GL_API_LOAD(glVertex3f,        FUNC_GL, LIB_GL32)
GL_API_LOAD(glFlush,           FUNC_GL, LIB_GL32)
GL_API_LOAD(glGetString,       FUNC_GL, LIB_GL32)
GL_API_LOAD(glGetIntegerv,     FUNC_GL, LIB_GL32)
GL_API_LOAD(wglMakeCurrent,    FUNC_WGL, LIB_GL32)
GL_API_LOAD(wglCreateContext,  FUNC_WGL, LIB_GL32)
GL_API_LOAD(wglDeleteContext,  FUNC_WGL, LIB_GL32)
GL_API_LOAD(wglGetProcAddress, FUNC_WINAPI, LIB_GL32)

GL_API_LOAD(SetPixelFormat,      FUNC_WINAPI, LIB_GDI32)
GL_API_LOAD(SwapBuffers,         FUNC_WINAPI, LIB_GDI32)
GL_API_LOAD(ChoosePixelFormat,   FUNC_WINAPI, LIB_GDI32)
GL_API_LOAD(DescribePixelFormat, FUNC_WINAPI, LIB_GDI32)

GL_API_LOAD(wglChoosePixelFormat,   FUNC_WRAP, LIB_GL32)
GL_API_LOAD(wglSetPixelFormat,      FUNC_WRAP, LIB_GL32)
GL_API_LOAD(wglSwapBuffers,         FUNC_WRAP, LIB_GL32)
GL_API_LOAD(wglDescribePixelFormat, FUNC_WRAP, LIB_GL32)

GL_API_LOAD(DrvSetPixelFormat,      FUNC_DRV, LIB_DRV)
GL_API_LOAD(DrvSwapBuffers,         FUNC_DRV, LIB_DRV)
GL_API_LOAD(DrvDescribePixelFormat, FUNC_DRV, LIB_DRV)
GL_API_LOAD(DrvGetProcAddress,      FUNC_DRV, LIB_DRV)
GL_API_LOAD(DrvSetContext,          FUNC_DRV, LIB_DRV)
GL_API_LOAD(DrvCreateLayerContext,  FUNC_DRV, LIB_DRV)
GL_API_LOAD(DrvReleaseContext,      FUNC_DRV, LIB_DRV)
