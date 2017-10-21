#include <string.h>
#include <stdlib.h>

#include "fpe_shader.h"
#include "string_utils.h"
#include "../glx/hardext.h"

const char* dummy_vertex = \
"varying vec4 Color; \n"
"void main() {\n"
"Color = gl_Color;\n"
"gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
"}";

const char* dummy_frag = \
"varying vec4 Color; \n"
"void main() {\n"
"gl_FragColor = Color;\n"
"}\n";

char* shad = NULL;
int shad_cap = 0;

#define ShadAppend(S) shad = Append(shad, &shad_cap, S)

const char* texvecsize[] = {"vec2", "vec3", "vec2"};
const char* texxyzsize[] = {"xy", "xyz", "xy"};
const char* texsampler[] = {"texture2D", "textureCube", "textureStream"};

const char* fpe_texenvSrc(int src, int tmu, int twosided) {
    static char buff[200];
    switch(src) {
        case FPE_SRC_TEXTURE:
            sprintf(buff, "texColor%d", tmu);
            break;
        case FPE_SRC_TEXTURE0:
        case FPE_SRC_TEXTURE1:
        case FPE_SRC_TEXTURE2:
        case FPE_SRC_TEXTURE3:
        case FPE_SRC_TEXTURE4:
        case FPE_SRC_TEXTURE5:
        case FPE_SRC_TEXTURE6:
        case FPE_SRC_TEXTURE7:
            sprintf(buff, "texColor%d", src-FPE_SRC_TEXTURE0);  // should check if texture is enabled
            break;
        case FPE_SRC_CONSTANT:
            sprintf(buff, "_gl4es_TextureEnvColor_%d", tmu);
            break;
        case FPE_SRC_PRIMARY_COLOR:
            sprintf(buff, "%s", twosided?"((gl_FrontFacing)?Color:BackColor)":"Color");
            break;
        case FPE_SRC_PREVIOUS:
            sprintf(buff, "fColor");
            break;
    }
    return buff;
}

const char* const* fpe_VertexShader(fpe_state_t *state) {
    // vertex is first called, so 1st time init is only here
    if(!shad_cap) shad_cap = 1024;
    if(!shad) shad = (char*)malloc(shad_cap);
    int lighting = state->lighting;
    int twosided = state->twosided && lighting;
    int light_separate = state->light_separate;
    int secondary = state->colorsum && !(lighting && light_separate);
    int headers = 0;
    char buff[1024];

    strcpy(shad, "varying vec4 Color;\n");  // might be unused...
    headers++;
    if(twosided) {
        ShadAppend("varying vec4 BackColor;\n");
        headers++;
    }
    if(light_separate || secondary) {
        ShadAppend("varying vec4 SecColor;\n");
        headers++;
        if(twosided) {
            ShadAppend("varying vec4 SecBackColor;\n");
            headers++;
        }
    }
    // textures coordinates
    for (int i=0; i<hardext.maxtex; i++) {
        int t = (state->texture>>(i*2))&0x3;
        if(t) {
            sprintf(buff, "varying %s _gl4es_TexCoord_%d;\n", texvecsize[t-1], i);
            ShadAppend(buff);
            headers++;
            if(state->textmat&(1<<i)) {
                sprintf(buff, "uniform mat4 _gl4es_TextureMatrix_%d;\n", i);
                ShadAppend(buff);
                headers++;
            }
        }
    }
    // let's start
    ShadAppend("\nvoid main() {\n");
    // initial Color / lighting calculation
    ShadAppend("gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n");
    if(!lighting) {
        ShadAppend("Color = gl_Color;\n");
        if(secondary) {
            ShadAppend("SecColor = gl_SecondaryColor;\n");
        }
    } else {
        ShadAppend("vec4 vertex = gl_ModelViewMatrix * gl_Vertex;\n");
        // material emission
        sprintf(buff, "Color = %s;\n", (state->cm_front_mode==FPE_CM_EMISSION)?"gl_Color":"gl_FrontMaterial.emission");
        ShadAppend(buff);
        if(twosided) {
            sprintf(buff, "vec4 BackColor = %s;\n", (state->cm_back_mode==FPE_CM_EMISSION)?"gl_Color":"gl_BackMaterial.emission");
            ShadAppend(buff);
        }
        sprintf(buff, "Color += %s*gl_FrontLightModelProduct.sceneColor;\n", 
            (state->cm_front_mode==FPE_CM_AMBIENT || state->cm_front_mode==FPE_CM_AMBIENTDIFFUSE)?"gl_Color":"gl_FrontMaterial.ambient");
        ShadAppend(buff);
        if(twosided) {
            sprintf(buff, "Color += %s*gl_BackLightModelProduct.sceneColor;\n", 
                (state->cm_back_mode==FPE_CM_AMBIENT || state->cm_back_mode==FPE_CM_AMBIENTDIFFUSE)?"gl_Color":"gl_BackMaterial.ambient");
            ShadAppend(buff);
        }
        if(light_separate) {
            ShadAppend("SecColor=vec4(0.);\n");
            if(twosided)
                ShadAppend("SecBackColor=vec4(0.);\n");
        }
        ShadAppend("float att;\n");
        ShadAppend("float spot;\n");
        ShadAppend("vec4 VP;\n");
        ShadAppend("float lVP;\n");
        ShadAppend("float nVP;\n");
        ShadAppend("float fi;\n");
        ShadAppend("vec4 aa,dd,ss;\n");
        ShadAppend("vec4 hi;\n");
        if(twosided)
            ShadAppend("vec4 back_aa,back_dd,back_ss;\n");
        ShadAppend("vec3 normal = gl_NormalMatrix * gl_Normal;\n");
        for(int i=0; i<hardext.maxlights; i++) {
            if(state->light&(1<<i)) {
                // enabled light i
                sprintf(buff, "VP = gl_LightSource[%d].position - vertex;\n", i);
                ShadAppend(buff);
                // att depend on light position w
                if((state->light_direction>>i&1)==0) {
                    ShadAppend("att = 1.0;\n");
                } else {
                    ShadAppend("lVP = length(VP);\n");
                    sprintf(buff, "att = 1.0/(gl_LightSource[%d].constantAttenuation + gl_LightSource[%d].linearAttenuation * lVP + gl_LightSource[%d].quadraticAttenuation * lVP*lVP);\n", i, i, i);
                    ShadAppend(buff);
                }
                ShadAppend("VP = normalize(VP);\n");
                // spot depend on spotlight cutoff angle
                if((state->light_cutoff180>>i&1)==0) {
                    //ShadAppend("spot = 1.0;\n");
                } else {
                    printf(buff, "spot = max(dot(VP.xyz, gl_LightSource[%d].spotDirection), 0.);\n", i);
                    ShadAppend(buff);
                    sprintf(buff, "if(spot<gl_LightSource[%d].spotCosCutoff) spot=0.0; else spot=pow(spot, gl_LightSource[%d].spotExponent);", i, i);
                    ShadAppend(buff);
                    ShadAppend("att *= spot;\n");
                }
                sprintf(buff, "nVP = max(dot(normal, VP.xyz), 0.);fi=(nVP!=0.)?1.:0.;\n");
                ShadAppend(buff);
                sprintf(buff, "aa = gl_FrontMaterial.ambient * gl_LightSource[%d].ambient;\n", i);
                ShadAppend(buff);
                if(twosided) {
                    sprintf(buff, "back_aa = gl_BackMaterial.ambient * gl_LightSource[%d].ambient;\n", i);
                    ShadAppend(buff);
                }
                sprintf(buff, "dd = nVP * gl_FrontMaterial.diffuse * gl_LightSource[%d].diffuse;\n", i);
                ShadAppend(buff);
                if(twosided) {
                    sprintf(buff, "back_dd = nVP * gl_BackMaterial.diffuse * gl_LightSource[%d].diffuse;\n", i);
                    ShadAppend(buff);
                }
                if(state->light_localviewer) {
                    ShadAppend("hi = VP + normalize(-V);\n");
                } else {
                    ShadAppend("hi = VP;\n");
                }
                sprintf(buff, "ss = fi*pow(max(dot(hi.xyz, normal),0.), gl_FrontMaterial.shininess)*gl_FrontMaterial.specular*gl_LightSource[%d].specular;\n", i);
                ShadAppend(buff);
                if(twosided) {
                    sprintf(buff, "ss = fi*pow(max(dot(hi.xyz, normal),0.), gl_BackMaterial.shininess)*gl_BackMaterial.specular*gl_LightSource[%d].specular;\n", i);
                    ShadAppend(buff);
                }
                if(state->light_separate) {
                    ShadAppend("Color += att*(aa+dd);\n");
                    ShadAppend("SecColor += att*(ss);\n");
                    if(twosided) {
                        ShadAppend("BackColor += att*(back_aa+back_dd);\n");
                        ShadAppend("SecBackColor += att*(back_ss);\n");
                    }
                } else {
                    ShadAppend("Color += att*(aa+dd+ss);\n");
                    if(twosided)
                        ShadAppend("BackColor += att*(back_aa+back_dd+back_ss);\n");
                }
                ShadAppend("Color.a = gl_FrontMaterial.diffuse.a;\n");
                if(twosided)
                    ShadAppend("BackColor.a = gl_BackMaterial.diffuse.a;\n");
            }
        }
    }
    // calculate texture coordinates
    for (int i=0; i<hardext.maxtex; i++) {
        int t = (state->texture>>(i*2))&0x3;
        int mat = state->textmat&(1<<i)?1:0;
        if(t) {
            if(mat)
                sprintf(buff, "_gl4es_TexCoord_%d = (gl_MultiTexCoord%d * _gl4es_TextureMatrix_%d).%s;\n", i, i, i, texxyzsize[t-1]);
            else
                sprintf(buff, "_gl4es_TexCoord_%d = gl_MultiTexCoord%d.%s;\n", i, i, texxyzsize[t-1]);
            ShadAppend(buff);
        }
    }

    ShadAppend("}\n");

    return (const char* const*)&shad;
}

const char* const* fpe_FragmentShader(fpe_state_t *state) {
    int headers = 0;
    int lighting = state->lighting;
    int twosided = state->twosided && lighting;
    int light_separate = state->light_separate && lighting;
    int secondary = state->colorsum && !(lighting && light_separate);
    int alpha_test = state->alphatest;
    int alpha_func = state->alphafunc;
    int texenv_combine = 0;
    char buff[100];

    strcpy(shad, "varying vec4 Color;\n");
    headers++;
    if(twosided) {
        ShadAppend("varying vec4 BackColor;\n");
        headers++;
    }
    if(light_separate || secondary) {
        ShadAppend("varying vec4 SecColor;\n");
        headers++;
        if(twosided) {
            ShadAppend("varying vec4 SecBackColor;\n");
            headers++;
        }
    }
    if(alpha_test && alpha_func>FPE_NEVER) {
        ShadAppend("uniform float _gl4es_AlphaRef;\n");
        headers++;
    }
    // textures coordinates
    for (int i=0; i<hardext.maxtex; i++) {
        int t = (state->texture>>(i*2))&0x3;
        if(t) {
            sprintf(buff, "varying %s _gl4es_TexCoord_%d;\n", texvecsize[t-1], i);
            ShadAppend(buff);
            sprintf(buff, "uniform sampler2D _gl4es_TexSampler_%d;\n", i);
            ShadAppend(buff);
            headers++;

            int texenv = (state->texenv>>(i*3))&0x07;
            if (texenv==FPE_COMBINE) {
                texenv_combine = 1;
                if((state->texrgbscale>>i)&1) {
                    sprintf(buff, "uniform float _gl4es_TexEnvRGBScale_%d;\n", i);
                    ShadAppend(buff);
                    headers++;
                }
                if((state->texalphascale>>i)&1) {
                    sprintf(buff, "uniform float _gl4es_TexEnvAlphaScale_%d;\n", i);
                    ShadAppend(buff);
                    headers++;
                }
            }
        }
    }

    ShadAppend("void main() {\n");
    //*** initial color
    sprintf(buff, "vec4 fColor = %s;\n", twosided?"(gl_FrontFacing)?Color:BackColor":"Color");
    ShadAppend(buff);

    //*** apply textures
    if(state->texture) {
        // fetch textures first
        for (int i=0; i<hardext.maxtex; i++) {
            int t = (state->texture>>(i*2))&0x3;
            if(t) {
                sprintf(buff, "lowp vec4 texColor%d = %s(_gl4es_TexSampler_%d, _gl4es_TexCoord_%d);\n", i, texsampler[t-1], i, i);
                ShadAppend(buff);
            }
        }

        // TexEnv stuff
        if(texenv_combine)
            ShadAppend("vec4 Arg0, Arg1, Arg2;\n");
        // fetch textures first
        for (int i=0; i<hardext.maxtex; i++) {
            int t = (state->texture>>(i*2))&0x3;
            if(t) {
                int texenv = (state->texenv>>(i*3))&0x07;
                int texformat = (state->texformat>>(i*3))&0x07;
                switch (texenv) {
                    case FPE_MODULATE:
                        sprintf(buff, "fColor *= texColor%d;\n", i);
                        ShadAppend(buff);
                        break;
                    case FPE_ADD:
                        if(texformat!=FPE_TEX_ALPHA) {
                            sprintf(buff, "fColor.rgb += texColor%d.rgb;\n", i);
                            ShadAppend(buff);
                        }
                        if(texformat==FPE_TEX_INTENSITY)
                            sprintf(buff, "fColor.a += texColor%d.a;\n", i);
                        else
                            sprintf(buff, "fColor.a *= texColor%d.a;\n", i);
                        ShadAppend(buff);
                        break;
                    case FPE_DECAL:
                        sprintf(buff, "fColor.rgb = fColor.rgb*(1.-texColor%d.a) + texColor%d.rgb*texColor%d.a;\n", i, i, i);
                        ShadAppend(buff);
                        break;
                    case FPE_BLEND:
                        if(texformat!=FPE_TEX_ALPHA) {
                            sprintf(buff, "fColor.rgb = fColor.rgb*(vec3(1.)-texColor%d.rgb) + texColor%d.rgb*texColor%d.rgb;\n", i, i, i);
                            ShadAppend(buff);
                        }
                        sprintf(buff, "fColor.a *= texColor%d.a;\n", i);
                        ShadAppend(buff);
                        break;
                    case FPE_REPLACE:
                        if(texformat==FPE_TEX_RGB || texformat!=FPE_TEX_LUM) {
                            sprintf(buff, "fColor.rgb = texColor%d.rgb;\n", i);
                            ShadAppend(buff);
                        } else if(texformat==FPE_TEX_ALPHA) {
                            sprintf(buff, "fColor.a = texColor%d.a;\n", i);
                            ShadAppend(buff);
                        } else {
                            sprintf(buff, "fColor = texColor%d;\n", i);
                            ShadAppend(buff);
                        }
                        break;
                    case FPE_COMBINE:
                        {
                            int constant = 0;
                            // parse the combine state
                            int combine_rgb = state->texcombine[i]&0xf;
                            int combine_alpha = (state->texcombine[i]>>4)&0xf;
                            int src_r[3], op_r[3];
                            int src_a[3], op_a[3];
                            for (int j=0; j<3; j++) {
                                src_a[j] = (state->texsrcalpha[j]>>(i*4))&0xf;
                                op_a[j] = (state->texopalpha[j]>>i)&1;
                                src_r[j] = (state->texsrcrgb[j]>>(i*4))&0xf;
                                op_r[j] = (state->texoprgb[j]>>(i*2))&3;
                            }
                            if(combine_rgb==FPE_CR_DOT3_RGBA) {
                                    src_a[0] = src_a[1] = src_a[2] = -1;
                                    op_a[0] = op_a[1] = op_a[2] = -1;
                                    src_r[2] = op_r[2] = -1;
                            } else {
                                if(combine_alpha==FPE_CR_REPLACE) {
                                    src_a[1] = src_a[2] = -1;
                                    op_a[1] = op_a[2] = -1;
                                } else if (combine_alpha!=FPE_CR_INTERPOLATE) {
                                    src_a[2] = op_a[2] = -1;
                                }
                                if(combine_rgb==FPE_CR_REPLACE) {
                                    src_r[1] = src_r[2] = -1;
                                    op_r[1] = op_r[2] = -1;
                                } else if (combine_rgb!=FPE_CR_INTERPOLATE) {
                                    src_r[2] = op_r[2] = -1;
                                }
                            }
                            // is texture constants needed ?
                            for (int j=0; j<3; j++) {
                                if (src_a[j]==FPE_SRC_CONSTANT)
                                    constant=1;
                            }
                            if(constant) {
                                // yep, create the Uniform
                                sprintf(buff, "uniform lowp vec4 _gl4es_TextureEnvColor_%d;\n", i);
                                shad = ResizeIfNeeded(shad, &shad_cap, strlen(buff));
                                InplaceInsert(GetLine(buff, headers), buff);
                                headers+=CountLine(buff);                            
                            }
                            for (int j=0; j<3; j++) {
                                if(op_r[j]!=-1)
                                switch(op_r[j]) {
                                    case FPE_OP_SRCCOLOR:
                                        sprintf(buff, "Arg%d.rgb = %s.rgb;\n", j, fpe_texenvSrc(src_r[0], i, twosided));
                                        break;
                                    case FPE_OP_MINUSCOLOR:
                                        sprintf(buff, "Arg%d.rgb = vec3(1.) - %s.rgb;\n", j, fpe_texenvSrc(src_r[0], i, twosided));
                                        break;
                                    case FPE_OP_ALPHA:
                                        sprintf(buff, "Arg%d.rgb = vec3(%s.a);\n", j, fpe_texenvSrc(src_r[0], i, twosided));
                                        break;
                                    case FPE_OP_MINUSALPHA:
                                        sprintf(buff, "Arg%d.rgb = vec3(1. - %s.a);\n", j, fpe_texenvSrc(src_r[0], i, twosided));
                                        break;
                                }
                                ShadAppend(buff);
                                if(op_a[j]!=-1)
                                switch(op_a[j]) {
                                    case FPE_OP_ALPHA:
                                        sprintf(buff, "Arg%d.a = %s.a;\n", j, fpe_texenvSrc(src_r[0], i, twosided));
                                        break;
                                    case FPE_OP_MINUSALPHA:
                                        sprintf(buff, "Arg%d.a = 1. - %s.a;\n", j, fpe_texenvSrc(src_r[0], i, twosided));
                                        break;
                                }
                                ShadAppend(buff);
                            }
                                
                            switch(combine_rgb) {
                                case FPE_CR_REPLACE:
                                    ShadAppend("fColor.rgb = Arg0.rgb;\n");
                                    break;
                                case FPE_CR_MODULATE:
                                    ShadAppend("fColor.rgb = Arg0.rgb * Arg1.rgb;\n");
                                    break;
                                case FPE_CR_ADD:
                                    ShadAppend("fColor.rgb = Arg0.rgb + Arg1.rgb;\n");
                                    break;
                                case FPE_CR_ADD_SIGNED:
                                    ShadAppend("fColor.rgb = Arg0.rgb + Arg1.rgb - vec3(0.5);\n");
                                    break;
                                case FPE_CR_INTERPOLATE:
                                    ShadAppend("fColor.rgb = Arg0.rgb*Arg2.rgb + Arg1.rgb*(vec3(1.)-Arg2.rgb);\n");
                                    break;
                                case FPE_CR_SUBTRACT:
                                    ShadAppend("fColor.rgb = Arg0.rgb - Arg1.rgb;\n");
                                    break;
                                case FPE_CR_DOT3_RGB:
                                    ShadAppend("fColor.rgb = vec3(4*((Arg0.r-0.5)*(Arg1.r-0.5)+(Arg0.g-0.5)*(Arg1.g-0.5)+(Arg0.b-0.5)*(Arg1.b-0.5)));\n");
                                    break;
                                case FPE_CR_DOT3_RGBA:
                                    ShadAppend("fColor = vec4(4*((Arg0.r-0.5)*(Arg1.r-0.5)+(Arg0.g-0.5)*(Arg1.g-0.5)+(Arg0.b-0.5)*(Arg1.b-0.5)));\n");
                                    break;
                            }
                            if(combine_rgb!=FPE_CR_DOT3_RGBA) 
                            switch(combine_alpha) {
                                case FPE_CR_REPLACE:
                                    ShadAppend("fColor.a = Arg0.a;\n");
                                    break;
                                case FPE_CR_MODULATE:
                                    ShadAppend("fColor.a = Arg0.a * Arg1.a;\n");
                                    break;
                                case FPE_CR_ADD:
                                    ShadAppend("fColor.a = Arg0.a + Arg1.a;\n");
                                    break;
                                case FPE_CR_ADD_SIGNED:
                                    ShadAppend("fColor.a = Arg0.a + Arg1.a - 0.5;\n");
                                    break;
                                case FPE_CR_INTERPOLATE:
                                    ShadAppend("fColor.a = Arg0.a*Arg2.a + Arg1.a*(1.-Arg2.a);\n");
                                    break;
                                case FPE_CR_SUBTRACT:
                                    ShadAppend("fColor.a = Arg0.a - Arg1.a;\n");
                                    break;
                            }
                            if((state->texrgbscale>>i)&1) {
                                sprintf(buff, "fColor.rgb *= _gl4es_TexEnvRGBScale_%d;\n", i);
                                ShadAppend(buff);
                            }
                            if((state->texalphascale>>i)&1) {
                                sprintf(buff, "fColor.a *= _gl4es_TexEnvAlphaScale_%d;\n", i);
                                ShadAppend(buff);
                            }
                        }
                        break;
                }
                ShadAppend("fColor = clamp(fColor, 0., 1.);\n");
            }
        }
    }
    //*** Alpha Test
    if(alpha_test) {
        if(alpha_func==GL_ALWAYS) {
            // nothing here...
        } else if (alpha_func==GL_NEVER) {
            ShadAppend("discard;\n"); // Never pass...
        } else {
            // FPE_LESS FPE_EQUAL FPE_LEQUAL FPE_GREATER FPE_NOTEQUAL FPE_GEQUAL
            // but need to negate the operator
            const char* alpha_test_op[] = {">=","!=",">","<=","==","<"}; 
            sprintf(buff, "if (int(fColor.a*255.) %s int(_gl4es_AlphaRef*255.)) discard;\n", alpha_test_op[alpha_func-FPE_LESS]);
            ShadAppend(buff);
        }
    }
    //*** Fog

    //*** Add secondary color
    if(light_separate || secondary) {
        sprintf(buff, "fColor += vec4((%s).rgb, 0.);\n", twosided?"(gl_FrontFacing)?SecColor:BackSecColor":"SecColor");
        ShadAppend(buff);
    }

    //done
    ShadAppend("gl_FragColor = fColor;\n");
    ShadAppend("}");

    return (const char* const*)&shad;
}

