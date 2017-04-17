#include <iostream>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "stb_image.h"
#include "stb_image_write.h"
#include "TextureTools.h"

#include "Core/DebugLog.h"
#include "Rendering/OpenGLContext.h"

#include "Rendering/GLTools.h"

namespace TextureTools {
    double log_2( double n )  
    {  
        return log( n ) / log( 2 );      // log(n)/log(2) is log_2. 
    }

	GLuint loadTexture(std::string fileName, TextureInfo* texInfo){

    	std::string fileString = std::string(fileName);
    	fileString = fileString.substr(fileString.find_last_of("/"));

    	int width, height, bytesPerPixel;
        stbi_set_flip_vertically_on_load(true);
        unsigned char *data = stbi_load(fileName.c_str(), &width, &height, &bytesPerPixel, 0);

        if(data == NULL){
        	DEBUGLOG->log("ERROR : Unable to open image " + fileString);
        	  return -1;}

        //create new texture
        GLuint textureHandle;
        glGenTextures(1, &textureHandle);
     
        //bind the texture
		OPENGLCONTEXT->bindTexture(textureHandle);
     
        //send image data to the new texture
        if (bytesPerPixel < 3) {
        	DEBUGLOG->log("ERROR : Unable to open image " + fileString);
            return -1;
        } else if (bytesPerPixel == 3){
            glTexImage2D(GL_TEXTURE_2D, 0,GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
        } else if (bytesPerPixel == 4) {
            glTexImage2D(GL_TEXTURE_2D, 0,GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        } else {
        	DEBUGLOG->log("Unknown format for bytes per pixel... Changed to \"4\"");
            glTexImage2D(GL_TEXTURE_2D, 0,GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        }

        //texture settings
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, true);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		
		OPENGLCONTEXT->bindTexture(0);

        stbi_image_free(data);
        DEBUGLOG->log( "SUCCESS: image loaded from " + fileString );

		if (texInfo != nullptr)
		{
			texInfo->bytesPerPixel = bytesPerPixel;
			texInfo->handle = textureHandle;
			texInfo->height = height;
			texInfo->width = width;
		}

        return textureHandle;
    }

	GLuint loadTextureFromResourceFolder(std::string fileName, TextureInfo* texInfo, std::string resourcesPath){
    	std::string filePath = resourcesPath + "/" + fileName;

		return loadTexture(filePath, texInfo);
    }

	GLuint loadCubemap(std::vector<std::string> faces,bool generateMipMaps)
	{
	GLuint textureID;
	glGenTextures(1, &textureID);

	int width,height,bytesPerPixel;

	unsigned char* image;
	
	OPENGLCONTEXT->bindTexture(textureID, GL_TEXTURE_CUBE_MAP);
	for(GLuint i = 0; i < faces.size(); i++)
	{
		stbi_set_flip_vertically_on_load(false);
        image = stbi_load(faces[i].c_str(), &width, &height, &bytesPerPixel, 0);
		        //send image data to the new texture
        if (bytesPerPixel < 3) {
			DEBUGLOG->log("ERROR : Unable to open image " + faces[i]);
            return -1;
        } else if (bytesPerPixel == 3){
            // if (generateMipMaps)
            // {
            //     int numMipmaps = (int) log_2( (float) std::max(width,height) );
            //     glTexStorage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, numMipmaps, GL_RGB, width, height);  
            //     glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, 0,0, width, height, GL_RGB, GL_UNSIGNED_BYTE, image);
            //     // glGenerateMipmap(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i);
            // }
            // else
            // {
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0,GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, image);
                // glGenerateMipmap(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i);

            // }

        } else if (bytesPerPixel == 4) {
            // if (generateMipMaps)
            // {
            //     int numMipmaps = (int) log_2( (float) std::max(width,height) );
            //     glTexStorage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, numMipmaps, GL_RGBA, width, height);  
            //     glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, 0,0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, image);
            // }
            // else
            // {
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
                // glGenerateMipmap(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i);
            // }
        } else {
            DEBUGLOG->log("Unknown format for bytes per pixel... Changed to \"4\"");

            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0,GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
        }
        stbi_image_free(image);
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if(generateMipMaps)
    {
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        checkGLError(true);
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
        checkGLError(true);
    }
    else
    {
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    OPENGLCONTEXT->bindTexture(0, GL_TEXTURE_CUBE_MAP);


    return textureID;
    }


    GLuint loadCubemapFromResourceFolder(std::vector<std::string> fileNames, bool generateMipMaps, std::string resourcesPath){
        for (unsigned int i = 0; i < fileNames.size(); i++)
        {
            fileNames[i] = resourcesPath + "/" + fileNames[i];
        }
        return loadCubemap(fileNames, generateMipMaps);
    }

    GLuint loadDefaultCubemap(bool generateMipMaps, std::string resourcesPath)
    {
        std::vector<std::string> cubeMapFiles;
        cubeMapFiles.push_back("cubemap/cloudtop_rt.tga");
        cubeMapFiles.push_back("cubemap/cloudtop_lf.tga");
        cubeMapFiles.push_back("cubemap/cloudtop_up.tga");
        cubeMapFiles.push_back("cubemap/cloudtop_dn.tga");
        cubeMapFiles.push_back("cubemap/cloudtop_bk.tga");
        cubeMapFiles.push_back("cubemap/cloudtop_ft.tga");
        return TextureTools::loadCubemapFromResourceFolder(cubeMapFiles, generateMipMaps, resourcesPath);
    }

	bool saveTexture(std::string fileName, GLuint texture)
	{

		int width, height = -1;
		OPENGLCONTEXT->activeTexture(GL_TEXTURE30);
		OPENGLCONTEXT->bindTexture(0, GL_TEXTURE_2D);
		OPENGLCONTEXT->bindTexture(texture, GL_TEXTURE_2D);

		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH,  &width);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
		//glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &comp);

		if (width == -1 || height == -1) 
		{
			DEBUGLOG->log("ERROR: couldn't retrieve width/height!"); return false;
		}

		std::vector<unsigned char> data(width * height * 4 );
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, &data[0]);

		// flip y
		for (int i = 0; i < height/2; i++)
		{
			// save top line
			std::vector<unsigned char> tmp(data.begin() +  ( (i) * (width * 4) ), data.begin() + ( (i+1) * (width * 4)) ); 

			// copy from bottom to top
			std::copy(data.begin() + ( (height - 1 - i) * (width * 4) ), data.begin() + ( (height - i) * (width * 4) ), data.begin() + ( (i) * (width * 4) ) );

			// coppy from top to bottom
			std::copy(tmp.begin(), tmp.end(),  data.begin() + ( (height - 1 - i) * (width * 4) ) );
		}

		int comp = 4;
		int stride_in_bytes = width * comp * sizeof(unsigned char);

		// *data points to top-left-most pixel
		// For PNG, "stride_in_bytes" is the distance in bytes from the first byte of
		// a row of pixels to the first byte of the next row of pixels.
		return stbi_write_png( fileName.c_str(), width, height, comp, &data[0], stride_in_bytes );
	}

	bool saveTextureArrayLayer(std::string fileName, GLuint texture, int layer)
	{
		int width, height, depth = -1;
		OPENGLCONTEXT->activeTexture(GL_TEXTURE30);
		OPENGLCONTEXT->bindTexture(0, GL_TEXTURE_2D_ARRAY);
		OPENGLCONTEXT->bindTexture(texture, GL_TEXTURE_2D_ARRAY);
		
		glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, 0, GL_TEXTURE_WIDTH,  &width);
		glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, 0, GL_TEXTURE_HEIGHT, &height);
		glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, 0, GL_TEXTURE_DEPTH, &depth);
		//glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &comp);

		if (width == -1 || height == -1|| depth == -1) 
		{
			DEBUGLOG->log("ERROR: couldn't retrieve width/height/depth!"); return false;
		}
		if ( layer < 0 || layer >= depth ) 
		{
			DEBUGLOG->log("ERROR: parameter 'layer' out of texture bounds!"); return false;
		}

		std::vector<unsigned char> data(width * height * depth * 4 );
		glGetTexImage(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, GL_UNSIGNED_BYTE, &data[0]);

		// flip y
		int offset = width * height * 4 * layer;
		for (int i = 0; i < height/2; i++)
		{
			// save top line
			std::vector<unsigned char> tmp(data.begin() +  ( (offset) + (i) * (width * 4) ), data.begin() + ( (offset) + (i+1) * (width * 4)) ); 

			// copy from bottom to top
			std::copy(data.begin() + (offset) + ( (height - 1 - i) * (width * 4) ), data.begin() + (offset) + ( (height - i) * (width * 4) ), data.begin() + (offset) + ( (i) * (width * 4) ) );

			// coppy from top to bottom
			std::copy(tmp.begin(), tmp.end(),  data.begin() + (offset) + ( (height - 1 - i) * (width * 4) ) );
		}

		int comp = 4;
		int stride_in_bytes = width * comp * sizeof(unsigned char);

		// *data points to top-left-most pixel
		// For PNG, "stride_in_bytes" is the distance in bytes from the first byte of
		// a row of pixels to the first byte of the next row of pixels.
		return stbi_write_png( fileName.c_str(), width, height, comp, &data[ (offset) ], stride_in_bytes );
	}

	bool saveTextureArray(std::string fileName, GLuint texture)
	{
		int width, height, depth = -1;
		OPENGLCONTEXT->activeTexture(GL_TEXTURE30);
		OPENGLCONTEXT->bindTexture(0, GL_TEXTURE_2D_ARRAY);
		OPENGLCONTEXT->bindTexture(texture, GL_TEXTURE_2D_ARRAY);
		
		glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, 0, GL_TEXTURE_WIDTH,  &width);
		glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, 0, GL_TEXTURE_HEIGHT, &height);
		glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, 0, GL_TEXTURE_DEPTH, &depth);
		//glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &comp);

		if (width == -1 || height == -1|| depth == -1) 
		{
			DEBUGLOG->log("ERROR: couldn't retrieve width/height/depth!"); return false;
		}

		std::vector<unsigned char> data(width * height * depth * 4 );
		glGetTexImage(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, GL_UNSIGNED_BYTE, &data[0]);

		bool success = true;
		int offset = width * height * 4;
		for (int j = 0; j < depth; j++)
		{
		// flip y
		for (int i = 0; i < height/2; i++)
		{
			// save top line
			std::vector<unsigned char> tmp(data.begin() +  ( (offset * j) + (i) * (width * 4) ), data.begin() + ( (offset * j) + (i+1) * (width * 4)) ); 

			// copy from bottom to top
			std::copy(data.begin() + (offset * j) + ( (height - 1 - i) * (width * 4) ), data.begin() + (offset * j) + ( (height - i) * (width * 4) ), data.begin() + (offset * j) + ( (i) * (width * 4) ) );

			// coppy from top to bottom
			std::copy(tmp.begin(), tmp.end(),  data.begin() + (offset * j) + ( (height - 1 - i) * (width * 4) ) );
		}

		int comp = 4;
		int stride_in_bytes = width * comp * sizeof(unsigned char);

		// *data points to top-left-most pixel
		// For PNG, "stride_in_bytes" is the distance in bytes from the first byte of
		// a row of pixels to the first byte of the next row of pixels.
		success &= stbi_write_png( (fileName + "_" + std::to_string(j) + ".png").c_str(), width, height, comp, &data[ (offset * j) ], stride_in_bytes );
		}
		return success;
	}
}

