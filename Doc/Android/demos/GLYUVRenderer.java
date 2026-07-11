// GLYUVRenderer.java
// Android OpenGL ES SurfaceTexture 零拷贝 YUV 渲染器 — 独立可用
// 详见 Doc/Android/04-OpenGLES渲染与Surface详解.md

package com.example.render;

import android.graphics.SurfaceTexture;
import android.opengl.GLES11Ext;
import android.opengl.GLES20;
import android.opengl.GLSurfaceView;
import android.view.Surface;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public class GLYUVRenderer implements GLSurfaceView.Renderer {

    private static final String VS =
        "uniform mat4 uTexMatrix;\n" +
        "attribute vec4 aPos; attribute vec4 aTex;\n" +
        "varying vec2 vTex;\n" +
        "void main() {\n" +
        "  gl_Position = aPos;\n" +
        "  vTex = (uTexMatrix * aTex).xy;\n" +
        "}";

    private static final String FS =
        "#extension GL_OES_EGL_image_external : require\n" +
        "precision mediump float;\n" +
        "uniform samplerExternalOES sTex;\n" +
        "varying vec2 vTex;\n" +
        "void main() {\n" +
        "  vec3 c = texture2D(sTex, vTex).rgb;\n" +
        "  gl_FragColor = vec4(c, 1.0);\n" +
        "}";

    private static final float[] VERTICES = {
        -1,-1, 0,1,  1,-1, 1,1,  -1,1, 0,0,
         1,-1, 1,1,  1, 1, 1,0,  -1,1, 0,0,
    };
    private FloatBuffer vb;

    private int program;
    private SurfaceTexture surfaceTexture;
    private Surface surface;
    private int texId;
    private float[] texMatrix = new float[16];
    private boolean frameReady;

    @Override
    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
        vb = ByteBuffer.allocateDirect(VERTICES.length * 4)
            .order(ByteOrder.nativeOrder()).asFloatBuffer().put(VERTICES);
        vb.position(0);

        program = createProgram(VS, FS);

        // 创建 OES 纹理
        int[] tex = new int[1];
        GLES20.glGenTextures(1, tex, 0);
        texId = tex[0];
        GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, texId);
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
            GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR);
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
            GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR);
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
            GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE);
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
            GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE);

        // ★ 创建 SurfaceTexture + 包装为 Surface
        surfaceTexture = new SurfaceTexture(texId);
        surfaceTexture.setOnFrameAvailableListener(st -> {
            synchronized (this) { frameReady = true; }
        });
        surface = new Surface(surfaceTexture);
    }

    @Override
    public void onDrawFrame(GL10 gl) {
        synchronized (this) {
            if (frameReady) {
                surfaceTexture.updateTexImage();             // ★ 更新到最新帧
                surfaceTexture.getTransformMatrix(texMatrix); // ★ 变换矩阵
                frameReady = false;
            }
        }

        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT);
        GLES20.glUseProgram(program);

        GLES20.glActiveTexture(GLES20.GL_TEXTURE0);
        GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, texId);
        GLES20.glUniform1i(GLES20.glGetUniformLocation(program, "sTex"), 0);
        GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(program, "uTexMatrix"),
            1, false, texMatrix, 0);

        int aPos = GLES20.glGetAttribLocation(program, "aPos");
        int aTex = GLES20.glGetAttribLocation(program, "aTex");
        GLES20.glEnableVertexAttribArray(aPos);
        GLES20.glEnableVertexAttribArray(aTex);
        vb.position(0);
        GLES20.glVertexAttribPointer(aPos, 4, GLES20.GL_FLOAT, false, 16, vb);
        vb.position(2);
        GLES20.glVertexAttribPointer(aTex, 4, GLES20.GL_FLOAT, false, 16, vb);

        GLES20.glDrawArrays(GLES20.GL_TRIANGLES, 0, 6);

        GLES20.glDisableVertexAttribArray(aPos);
        GLES20.glDisableVertexAttribArray(aTex);
    }

    @Override
    public void onSurfaceChanged(GL10 gl, int w, int h) {
        GLES20.glViewport(0, 0, w, h);
    }

    public Surface getSurface() { return surface; }

    private int createProgram(String vs, String fs) {
        int v = loadShader(GLES20.GL_VERTEX_SHADER, vs);
        int f = loadShader(GLES20.GL_FRAGMENT_SHADER, fs);
        int p = GLES20.glCreateProgram();
        GLES20.glAttachShader(p, v); GLES20.glAttachShader(p, f);
        GLES20.glLinkProgram(p);
        return p;
    }

    private int loadShader(int type, String src) {
        int s = GLES20.glCreateShader(type);
        GLES20.glShaderSource(s, src); GLES20.glCompileShader(s);
        return s;
    }
}
