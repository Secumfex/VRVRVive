#include "AssimpTools.h"

#include "Core/DebugLog.h"

#include "Rendering/VertexArrayObjects.h"
#include "Rendering/OpenGLContext.h"

glm::vec3 toVec3(const aiVector3D& vert)
{
	return glm::vec3(vert.x, vert.y, vert.z);
}


std::vector<AssimpTools::RenderableInfo > AssimpTools::createSimpleRenderablesFromScene(const aiScene* scene,const glm::mat4& vertexTransform, bool createTangentsAndBitangents)
{
	std::vector<RenderableInfo >resultVector; 
	for (unsigned int i = 0; i < scene->mNumMeshes; i++)
	{
		aiMesh* m = scene->mMeshes[i];

		// mesh has vertices
		if(m->HasPositions())
		{
			// read info
			std::vector<unsigned int> indices;
			std::vector<float> vertices;
			std::vector<float> uvs;
			std::vector<float> normals;
			std::vector<float> tangents;
			//std::vector<float> bitangents;

			glm::vec3 min( FLT_MAX);
			glm::vec3 max(-FLT_MAX);

			auto checkMax = [](glm::vec3& max, const glm::vec3& vert) 
			{
				max.x = std::max(max.x, vert.x);
				max.y = std::max(max.y, vert.y);
				max.z = std::max(max.z, vert.z);
			};

			auto checkMin = [](glm::vec3& min, const glm::vec3& vert) 
			{
				min.x = std::min(min.x, vert.x);
				min.y = std::min(min.y, vert.y);
				min.z = std::min(min.z, vert.z);
			};
			
			if ( m->HasPositions()){
			for ( unsigned int v = 0; v < m->mNumVertices; v++)
			{
				glm::vec3 vert = toVec3( m->mVertices[v] );
				vert = glm::vec3( vertexTransform * glm::vec4(vert, 1.0f));

				vertices.push_back(vert.x);
				vertices.push_back(vert.y);
				vertices.push_back(vert.z);
				
				checkMin(min,vert);
				checkMax(max,vert);
			}}

			if ( m->HasNormals()){
			for ( unsigned int n = 0; n < m->mNumVertices; n++)
			{

				glm::vec3 norm = toVec3( m->mNormals[n] );
				norm = glm::normalize(glm::vec3( glm::transpose(glm::inverse(vertexTransform)) * glm::vec4(norm, 0.0f)));
				//DEBUGLOG->log("normal  " + DebugLog::to_string(n) + ": ", norm); 
				//DEBUGLOG->log("nrmlgth " + DebugLog::to_string(n) + ": ", glm::length(norm)); 
				normals.push_back(norm.x);
				normals.push_back(norm.y);
				normals.push_back(norm.z);
			}}

			if ( m->HasTextureCoords(0)){
			for (unsigned int u = 0; u < m->mNumVertices; u++)
			{
				aiVector3D uv = m->mTextureCoords[0][u];
				uvs.push_back(uv.x);
				uvs.push_back(uv.y);
				
				if(m->GetNumUVChannels() == 3)
				{
					uvs.push_back(uv.z);
				}
			}}


			if ( m->HasTangentsAndBitangents() && createTangentsAndBitangents){
			for (unsigned int t = 0; t < m->mNumVertices; t++)
			{
				glm::vec3 tangent = toVec3( m->mTangents[t]);
				tangent = glm::normalize(glm::vec3( glm::transpose(glm::inverse(vertexTransform)) * glm::vec4(tangent, 0.0f)));
				
				tangents.push_back(tangent.x);
				tangents.push_back(tangent.y);
				tangents.push_back(tangent.z);

				//glm::vec3 biTangent = toVec3( m->mBitangents[t]);
				//biTangent = glm::vec3( glm::transpose(glm::inverse(vertexTransform)) * glm::vec4(biTangent, 0.0f));
				//bitangents.push_back(biTangent.x);
				//bitangents.push_back(biTangent.y);
				//bitangents.push_back(biTangent.z);

			}}

			if ( m->HasFaces()){
			for ( unsigned int f = 0; f < m->mNumFaces; f++)
			{
				aiFace face = m->mFaces[f];
				for ( unsigned int idx = 0; idx < face.mNumIndices; idx++) 
				{
					indices.push_back(face.mIndices[idx]);
				}
			}}

			// method to upload data to GPU and set as vertex attribute
			auto createVbo = [](std::vector<float>& content, GLuint dimensions, GLuint attributeIndex)
			{ 
				GLuint vbo = 0;
				glGenBuffers(1, &vbo);
				glBindBuffer(GL_ARRAY_BUFFER, vbo);
				glBufferData(GL_ARRAY_BUFFER, content.size() * sizeof(float), &content[0], GL_STATIC_DRAW);
				glVertexAttribPointer(attributeIndex, dimensions, GL_FLOAT, 0, 0, 0);
				glEnableVertexAttribArray(attributeIndex);
				return vbo;
			};

			auto createIndexVbo = [](std::vector<unsigned int>& content) 
			{
				GLuint vbo = 0;	
				glGenBuffers(1, &vbo);
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vbo);
				glBufferData(GL_ELEMENT_ARRAY_BUFFER, content.size() * sizeof(unsigned int), &content[0], GL_STATIC_DRAW);
				return vbo;
			};

			// generate VAO
			GLuint vao;
		    glGenVertexArrays(1, &vao);
			Renderable *renderable = new Renderable;
			renderable->m_vao = vao;
			OPENGLCONTEXT->bindVAO(vao);

			if (m->HasPositions()){
			renderable->m_positions.m_vboHandle = createVbo(vertices, 3, 0);
			renderable->m_positions.m_size = vertices.size() / 3;
			}

			if(m->HasTextureCoords(0)){
			renderable->m_uvs.m_vboHandle = createVbo(uvs, (m->GetNumUVChannels() == 3) ? 3 : 2, 1);
			renderable->m_uvs.m_size = (m->GetNumUVChannels() == 3) ? uvs.size() / 3 : uvs.size() / 2;
			}

			if( m->HasNormals()){
			renderable->m_normals.m_vboHandle = createVbo(normals, 3, 2);
			renderable->m_normals.m_size = normals.size() / 3;
			}

			if (m->HasTangentsAndBitangents() && createTangentsAndBitangents)
			{
				renderable->m_tangents.m_vboHandle = createVbo(tangents, 3, 3);
				renderable->m_tangents.m_size = tangents.size() / 3;
			}

			// // commented out, because, like, just compute this in the shader
			//if (m->HasTangentsAndBitangents() && createTangentsAndBitangents)
			//{
			//	renderable->m_bitangents.m_vboHandle = createVbo(bitangents, 3, 4);
			//	renderable->m_bitangents.m_size =bitangents.size() /3 ;
			//}

			renderable->m_indices.m_vboHandle = createIndexVbo(indices);
			renderable->m_indices.m_size = indices.size();

			renderable->setDrawMode(GL_TRIANGLES);

			OPENGLCONTEXT->bindVAO(0);

			// save mesh info
			RenderableInfo renderableInfo;
			renderableInfo.renderable = renderable;
			renderableInfo.boundingBox.min = min;
			renderableInfo.boundingBox.max = max;
			renderableInfo.name = std::string( m->mName.C_Str() );
			renderableInfo.meshIdx = i;
			
			// add to set of renderables
			resultVector.push_back(renderableInfo);
		}
		return resultVector;
	}
}

std::vector<AssimpTools::VertexData> AssimpTools::createVertexDataInstancesFromScene( const aiScene* scene, const glm::mat4& vertexTransform, bool createTangentsAndBitangents)
{
	std::vector<VertexData >resultVector;
	resultVector.resize(scene->mNumMeshes);

	for (unsigned int i = 0; i < scene->mNumMeshes; i++)
	{
		aiMesh* m = scene->mMeshes[i];

		// mesh has vertices
		if(m->HasPositions())
		{
			// read info
			std::vector<unsigned int> indices;
			std::vector<float> vertices;
			std::vector<float> uvs;
			std::vector<float> normals;
			std::vector<float> tangents;
			//std::vector<float> bitangents;

			glm::vec3 min( FLT_MAX);
			glm::vec3 max(-FLT_MAX);

			auto checkMax = [](glm::vec3& max, const glm::vec3& vert) 
			{
				max.x = std::max(max.x, vert.x);
				max.y = std::max(max.y, vert.y);
				max.z = std::max(max.z, vert.z);
			};

			auto checkMin = [](glm::vec3& min, const glm::vec3& vert) 
			{
				min.x = std::min(min.x, vert.x);
				min.y = std::min(min.y, vert.y);
				min.z = std::min(min.z, vert.z);
			};
			
			if ( m->HasPositions()){
			for ( unsigned int v = 0; v < m->mNumVertices; v++)
			{
				glm::vec3 vert = toVec3( m->mVertices[v] );
				vert = glm::vec3( vertexTransform * glm::vec4(vert, 1.0f));

				vertices.push_back(vert.x);
				vertices.push_back(vert.y);
				vertices.push_back(vert.z);
				
				checkMin(min,vert);
				checkMax(max,vert);
			}}

			if ( m->HasNormals()){
			for ( unsigned int n = 0; n < m->mNumVertices; n++)
			{

				glm::vec3 norm = toVec3( m->mNormals[n] );
				norm = glm::normalize(glm::vec3( glm::transpose(glm::inverse(vertexTransform)) * glm::vec4(norm, 0.0f)));
				//DEBUGLOG->log("normal  " + DebugLog::to_string(n) + ": ", norm); 
				//DEBUGLOG->log("nrmlgth " + DebugLog::to_string(n) + ": ", glm::length(norm)); 
				normals.push_back(norm.x);
				normals.push_back(norm.y);
				normals.push_back(norm.z);
			}}

			if ( m->HasTextureCoords(0)){
			for (unsigned int u = 0; u < m->mNumVertices; u++)
			{
				aiVector3D uv = m->mTextureCoords[0][u];
				uvs.push_back(uv.x);
				uvs.push_back(uv.y);
				
				if(m->GetNumUVChannels() == 3)
				{
					uvs.push_back(uv.z);
				}
			}}


			if ( m->HasTangentsAndBitangents() && createTangentsAndBitangents){
			for (unsigned int t = 0; t < m->mNumVertices; t++)
			{
				glm::vec3 tangent = toVec3( m->mTangents[t]);
				tangent = glm::normalize(glm::vec3( glm::transpose(glm::inverse(vertexTransform)) * glm::vec4(tangent, 0.0f)));
				
				tangents.push_back(tangent.x);
				tangents.push_back(tangent.y);
				tangents.push_back(tangent.z);

				//glm::vec3 biTangent = toVec3( m->mBitangents[t]);
				//biTangent = glm::vec3( glm::transpose(glm::inverse(vertexTransform)) * glm::vec4(biTangent, 0.0f));
				//bitangents.push_back(biTangent.x);
				//bitangents.push_back(biTangent.y);
				//bitangents.push_back(biTangent.z);

			}}

			unsigned int maxIdx = 0;
			if ( m->HasFaces()){
			for ( unsigned int f = 0; f < m->mNumFaces; f++)
			{
				aiFace face = m->mFaces[f];
				for ( unsigned int idx = 0; idx < face.mNumIndices; idx++) 
				{
					indices.push_back(face.mIndices[idx]);
					maxIdx = std::max(face.mIndices[idx], maxIdx);
				}
			}}

			//DEBUGLOG->log("max index", maxIdx);

			resultVector[i].indices = indices;
			resultVector[i].positions = vertices;
			resultVector[i].uvs = uvs;
			resultVector[i].normals = normals;
			resultVector[i].tangents = tangents;
		}
	}
	return resultVector;
}

std::vector<Renderable* > AssimpTools::createSimpleRenderablesFromVertexDataInstances(std::vector<AssimpTools::VertexData>& vertexDataInstances)
{
	std::vector<Renderable* > resultVector;
	for ( int i = 0; i  < vertexDataInstances.size(); i++)
	{
		// generate VAO
		GLuint vao;
		glGenVertexArrays(1, &vao);
		Renderable *renderable = new Renderable;
		renderable->m_vao = vao;
		OPENGLCONTEXT->bindVAO(vao);

		if (!vertexDataInstances[i].positions.empty()){
		renderable->m_positions.m_vboHandle = Renderable::createVbo(vertexDataInstances[i].positions, 3, 0);
		renderable->m_positions.m_size = vertexDataInstances[i].positions.size() / 3;
		}

		if(!vertexDataInstances[i].uvs.empty()){
		renderable->m_uvs.m_vboHandle = Renderable::createVbo(vertexDataInstances[i].uvs, (vertexDataInstances[i].uvs.size() == vertexDataInstances[i].positions.size() ) ? 3 : 2, 1);
		renderable->m_uvs.m_size = ((vertexDataInstances[i].uvs.size() == vertexDataInstances[i].positions.size() ) == 3) ? vertexDataInstances[i].uvs.size() / 3 : vertexDataInstances[i].uvs.size() / 2;
		}

		if(!vertexDataInstances[i].normals.empty()){
		renderable->m_normals.m_vboHandle = Renderable::createVbo(vertexDataInstances[i].normals, 3, 2);
		renderable->m_normals.m_size = vertexDataInstances[i].normals.size() / 3;
		}

		if (!vertexDataInstances[i].tangents.empty())
		{
			renderable->m_tangents.m_vboHandle = Renderable::createVbo(vertexDataInstances[i].tangents, 3, 3);
			renderable->m_tangents.m_size = vertexDataInstances[i].tangents.size() / 3;
		}

		if (!vertexDataInstances[i].indices.empty())
		{
			renderable->m_indices.m_vboHandle = Renderable::createIndexVbo(vertexDataInstances[i].indices);
			renderable->m_indices.m_size = vertexDataInstances[i].indices.size();
		}

		renderable->setDrawMode(GL_TRIANGLES);

		OPENGLCONTEXT->bindVAO(0);

		// add to set of renderables
		resultVector.push_back(renderable);
	}
	return resultVector;
}

AssimpTools::BoundingBox AssimpTools::computeBoundingBox(const aiMesh* mesh)
{
	glm::vec3 min( FLT_MAX);
	glm::vec3 max(-FLT_MAX);

	auto checkMax = [](glm::vec3& max, const aiVector3D& vert) 
	{
		max.x = std::max(max.x, vert.x);
		max.y = std::max(max.y, vert.y);
		max.z = std::max(max.z, vert.z);
	};

	auto checkMin = [](glm::vec3& min, const aiVector3D& vert) 
	{
		min.x = std::min(min.x, vert.x);
		min.y = std::min(min.y, vert.y);
		min.z = std::min(min.z, vert.z);
	};

	for ( unsigned int v = 0; v < mesh->mNumVertices; v++)
	{
		aiVector3D vert = mesh->mVertices[v];
				
		checkMin(min,vert);
		checkMax(max,vert);
	}

	BoundingBox result = {min,max};
	return result;
}

AssimpTools::BoundingBox AssimpTools::computeBoundingBox(VertexData& mesh)
{
	glm::vec3 min( FLT_MAX);
	glm::vec3 max(-FLT_MAX);

	auto checkMax = [](glm::vec3& max, glm::vec3& vert) 
	{
		max.x = std::max(max.x, vert.x);
		max.y = std::max(max.y, vert.y);
		max.z = std::max(max.z, vert.z);
	};

	auto checkMin = [](glm::vec3& min, glm::vec3&vert) 
	{
		min.x = std::min(min.x, vert.x);
		min.y = std::min(min.y, vert.y);
		min.z = std::min(min.z, vert.z);
	};

	for ( unsigned int v = 0; v < mesh.positions.size(); v = v+3)
	{
		glm::vec3 vert(mesh.positions[v], mesh.positions[v+1], mesh.positions[v+2]);
				
		checkMin(min,vert);
		checkMax(max,vert);
	}

	BoundingBox result = {min,max};
	return result;
}

void AssimpTools::checkMin(glm::vec3& min, const glm::vec3& point)
{
	min.x = std::min(min.x, point.x);
	min.y = std::min(min.y, point.y);
	min.z = std::min(min.z, point.z);
}

void AssimpTools::checkMax(glm::vec3& max, const glm::vec3& point)
{
	max.x = std::max(max.x, point.x);
	max.y = std::max(max.y, point.y);
	max.z = std::max(max.z, point.z);
}


#include <assimp/Importer.hpp>
const aiScene* AssimpTools::importAssetFromResourceFolder(std::string filename, Assimp::Importer& importer,int steps, std::string resourcesPath)
{
	const aiScene* scene = importer.ReadFile( resourcesPath + "/" + filename, steps);
	
	if (scene == NULL)
	{
		std::string errorString = importer.GetErrorString();
		DEBUGLOG->log("ERROR: " + errorString);
	} else {
		DEBUGLOG->log("Model has been loaded successfully");
	}

	return scene;
}

std::unordered_map<aiTextureType, AssimpTools::MaterialTextureInfo, AssimpTools::EnumClassHash> AssimpTools::getMaterialTexturesInfo(const aiScene* scene, int matIdx)
{
	std::unordered_map<aiTextureType, AssimpTools::MaterialTextureInfo, AssimpTools::EnumClassHash> result;
	
	if (matIdx >= scene->mNumMaterials)
	{
		DEBUGLOG->log("ERROR: invalid material index");
		return result;
	}

	auto m = scene->mMaterials[matIdx];

	for (int t = aiTextureType::aiTextureType_NONE; t <= aiTextureType::aiTextureType_UNKNOWN; t++)
	{
		if (m->GetTextureCount( (aiTextureType) t ) != 0)
		{
			aiString path;
			m->GetTexture( (aiTextureType) t, 0, &path);
			std::string sPath(path.C_Str());
			
			MaterialTextureInfo info = {matIdx, t, sPath}; 
			result[(aiTextureType) t] = info;
		}
	}
	return result;
}

AssimpTools::MaterialInfo AssimpTools::getMaterialInfo(const aiScene* scene, int matIdx)
{
	AssimpTools::MaterialInfo result;
	result.matIdx = matIdx;

	if (matIdx >= scene->mNumMaterials)
	{
		DEBUGLOG->log("ERROR: invalid material index");
		return result;
	}

	auto m = scene->mMaterials[matIdx];

    aiColor4D temp;
    if(AI_SUCCESS == aiGetMaterialColor(m, AI_MATKEY_COLOR_DIFFUSE, &temp))
		result.color[DIFFUSE] = glm::vec4(temp.r, temp.g, temp.b, temp.a);
    if(AI_SUCCESS == aiGetMaterialColor(m, AI_MATKEY_COLOR_AMBIENT, &temp))
        result.color[AMBIENT]= glm::vec4(temp.r, temp.g, temp.b, temp.a);
    if(AI_SUCCESS == aiGetMaterialColor(m, AI_MATKEY_COLOR_SPECULAR, &temp))
        result.color[SPECULAR]= glm::vec4(temp.r, temp.g, temp.b, temp.a);
    if(AI_SUCCESS == aiGetMaterialColor(m, AI_MATKEY_COLOR_EMISSIVE, &temp))
        result.color[EMISSIVE]= glm::vec4(temp.r, temp.g, temp.b, temp.a);
    if(AI_SUCCESS == aiGetMaterialColor(m, AI_MATKEY_COLOR_REFLECTIVE, &temp))
        result.color[REFLECTIVE]= glm::vec4(temp.r, temp.g, temp.b, temp.a);
    if(AI_SUCCESS == aiGetMaterialColor(m, AI_MATKEY_COLOR_TRANSPARENT, &temp))
        result.color[TRANSPARENT_]= glm::vec4(temp.r, temp.g, temp.b, temp.a);

    float scalar;
    if(AI_SUCCESS == aiGetMaterialFloat(m, AI_MATKEY_SHININESS, &scalar))
		result.scalar[SHININESS]= scalar;
    if(AI_SUCCESS == aiGetMaterialFloat(m, AI_MATKEY_OPACITY, &scalar))
        result.scalar[OPACITY] =scalar;
    if(AI_SUCCESS == aiGetMaterialFloat(m, AI_MATKEY_SHININESS_STRENGTH, &scalar))
        result.scalar[SHININESS_STRENGTH]= scalar;
	if(AI_SUCCESS == aiGetMaterialFloat(m, AI_MATKEY_REFLECTIVITY, &scalar))
		result.scalar[REFLECTIVITY]= scalar;

	result.texture = getMaterialTexturesInfo(scene, matIdx);

	return result;
}

std::string AssimpTools::decodeAiTextureType(aiTextureType type)
{
	switch (type)
	{
	case aiTextureType_AMBIENT: return std::string("AMBIENT");
	case aiTextureType_DIFFUSE: return std::string("DIFFUSE");
	case aiTextureType_DISPLACEMENT: return std::string("DISPLACEMENT");
	case aiTextureType_EMISSIVE: return std::string("EMISSIVE");
	case aiTextureType_HEIGHT: return std::string("HEIGHT");
	case aiTextureType_LIGHTMAP: return std::string("LIGHTMAP");
	case aiTextureType_NONE: return std::string("NONE");
	case aiTextureType_NORMALS: return std::string("NORMALS");
	case aiTextureType_OPACITY: return std::string("OPACITY");
	case aiTextureType_REFLECTION: return std::string("REFLECTION");
	case aiTextureType_SHININESS: return std::string("SHININESS");
	case aiTextureType_SPECULAR: return std::string("SPECULAR");
	default: case aiTextureType_UNKNOWN: return std::string("UNKNOWN");
	}
}

std::string AssimpTools::decodeScalarType(ScalarType type)
{
	switch (type)
	{
	case OPACITY: return std::string("AMBIENT");
	case SHININESS: return std::string("SHININESS (exponent)");
	case SHININESS_STRENGTH: return std::string("SHININESS_STRENGTH");
	case REFLECTIVITY: return std::string("REFLECTIVITY");
	default: return std::string("UNKNOWN");
	}
}

std::string AssimpTools::decodeColorType(ColorType type)
{
	switch (type)
	{
	case AMBIENT: return std::string("AMBIENT");
	case DIFFUSE: return std::string("DIFFUSE");
	case EMISSIVE: return std::string("EMISSIVE");
	case REFLECTIVE: return std::string("REFLECTIVE");
	case TRANSPARENT_: return std::string("TRANSPARENT");
	case SPECULAR: return std::string("SPECULAR");
	default: std::string("UNKNOWN");
	}
}

void AssimpTools::printMaterialInfo(const MaterialInfo& materialInfo)
{
	DEBUGLOG->log("Material idx: " , materialInfo.matIdx);
	DEBUGLOG->log("scalars"); DEBUGLOG->indent();
	for (auto e : materialInfo.scalar)
	{
		DEBUGLOG->log(decodeScalarType(e.first) +  std::string(": ") , e.second);
	}
	DEBUGLOG->outdent();
	
	DEBUGLOG->log("colors"); DEBUGLOG->indent();
	for (auto e : materialInfo.color)
	{
		DEBUGLOG->log(decodeColorType(e.first) +  std::string(": ") , e.second);
	}
	DEBUGLOG->outdent();
	
	DEBUGLOG->log("textures"); DEBUGLOG->indent();
	for (auto e : materialInfo.texture)
	{
		DEBUGLOG->log(decodeAiTextureType(e.first) + ": " +  e.second.relativePath);
	}
	DEBUGLOG->outdent();
}
