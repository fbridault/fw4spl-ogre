/* ***** BEGIN LICENSE BLOCK *****
 * FW4SPL - Copyright (C) IRCAD, 2014-2015.
 * Distributed under the terms of the GNU Lesser General Public License (LGPL) as
 * published by the Free Software Foundation.
 * ****** END LICENSE BLOCK ****** */

#include "visuOgreAdaptor/SMesh.hpp"

//#define FW_PROFILING_DISABLED

#include <fwCore/Profiling.hpp>

#include <fwCom/Signal.hxx>
#include <fwCom/Slots.hxx>

#include <fwComEd/helper/Mesh.hpp>

#include <fwData/mt/ObjectReadLock.hpp>
#include <fwDataTools/Mesh.hpp>

#include <fwServices/macros.hpp>
#include <fwServices/Base.hpp>

#include <OGRE/OgreAxisAlignedBox.h>

#include "visuOgreAdaptor/SMaterial.hpp"

#include <cstdint>

fwServicesRegisterMacro( ::fwRenderOgre::IAdaptor, ::visuOgreAdaptor::SMesh, ::fwData::Mesh );

//-----------------------------------------------------------------------------

namespace visuOgreAdaptor
{

const unsigned int SMesh::s_maxTextureSize;

//-----------------------------------------------------------------------------

static const ::fwCom::Slots::SlotKeyType s_UPDATE_VISIBILITY_SLOT       = "updateVisibility";
static const ::fwCom::Slots::SlotKeyType s_MODIFY_MESH_SLOT             = "modifyMesh";
static const ::fwCom::Slots::SlotKeyType s_MODIFY_COLORS_SLOT           = "modifyColors";
static const ::fwCom::Slots::SlotKeyType s_MODIFY_POINT_TEX_COORDS_SLOT = "modifyTexCoords";
static const ::fwCom::Slots::SlotKeyType s_MODIFY_VERTICES_SLOT         = "modifyVertices";

//-----------------------------------------------------------------------------

template <typename T>
void copyIndices(void* _pTriangles, void* _pQuads, void* _pEdges, void* _pTetras,
                 ::fwComEd::helper::Mesh& _meshHelper, size_t uiNumCells)
{
    FW_PROFILE_AVG("copyIndices", 5);

    T* pTriangles = static_cast<T*>(_pTriangles);
    T* pQuads     = static_cast<T*>(_pQuads);
    T* pEdges     = static_cast<T*>(_pEdges);
    T* pTetras    = static_cast<T*>(_pTetras);

    ::fwData::Mesh::CellDataMultiArrayType cells                  = _meshHelper.getCellData();
    ::fwData::Mesh::CellDataOffsetsMultiArrayType cellDataOffsets = _meshHelper.getCellDataOffsets();
    ::fwData::Mesh::CellTypesMultiArrayType cellsType             = _meshHelper.getCellTypes();

    for (unsigned int i = 0; i < uiNumCells; ++i)
    {
        long offset = static_cast<long>(cellDataOffsets[i]);
        if ( cellsType[i] == ::fwData::Mesh::TRIANGLE )
        {
            *pTriangles++ = static_cast<T>(cells[offset]);
            *pTriangles++ = static_cast<T>(cells[offset + 1]);
            *pTriangles++ = static_cast<T>(cells[offset + 2]);
        }
        else if ( cellsType[i] == ::fwData::Mesh::QUAD )
        {
            *pQuads++ = static_cast<T>(cells[offset]);
            *pQuads++ = static_cast<T>(cells[offset + 1]);
            *pQuads++ = static_cast<T>(cells[offset + 2]);

            *pQuads++ = static_cast<T>(cells[offset]);
            *pQuads++ = static_cast<T>(cells[offset + 2]);
            *pQuads++ = static_cast<T>(cells[offset + 3]);
        }
        else if ( cellsType[i] == ::fwData::Mesh::EDGE )
        {
            *pEdges++ = static_cast<T>(cells[offset]);
            *pEdges++ = static_cast<T>(cells[offset + 1]);
        }
        else if ( cellsType[i] == ::fwData::Mesh::TETRA )
        {
            *pTetras++ = static_cast<T>(cells[offset]);
            *pTetras++ = static_cast<T>(cells[offset + 1]);

            *pTetras++ = static_cast<T>(cells[offset + 1]);
            *pTetras++ = static_cast<T>(cells[offset + 2]);

            *pTetras++ = static_cast<T>(cells[offset + 2]);
            *pTetras++ = static_cast<T>(cells[offset + 3]);

            *pTetras++ = static_cast<T>(cells[offset + 3]);
            *pTetras++ = static_cast<T>(cells[offset]);

            *pTetras++ = static_cast<T>(cells[offset + 2]);
            *pTetras++ = static_cast<T>(cells[offset]);

            *pTetras++ = static_cast<T>(cells[offset + 1]);
            *pTetras++ = static_cast<T>(cells[offset + 3]);
        }
    }
}

//-----------------------------------------------------------------------------

SMesh::SMesh() throw() :
    m_autoResetCamera(true),
    m_entity(nullptr),
    m_materialAdaptorUID(""),
    m_materialTemplateName(SMaterial::DEFAULT_MATERIAL_TEMPLATE_NAME),
    m_meshName(""),
    m_texAdaptorUID(""),
    m_hasNormal(false),
    m_hasVertexColor(false),
    m_hasPrimitiveColor(false),
    m_isDynamic(false),
    m_isDynamicVertices(false),
    m_hasUV(false),
    m_isReconstructionManaged(false),
    m_useNewMaterialAdaptor(false)
{
    m_material = ::fwData::Material::New();

    newSlot(s_UPDATE_VISIBILITY_SLOT, &SMesh::updateVisibility, this);
    newSlot(s_MODIFY_MESH_SLOT, &SMesh::modifyMesh, this);
    newSlot(s_MODIFY_COLORS_SLOT, &SMesh::modifyPointColors, this);
    newSlot(s_MODIFY_POINT_TEX_COORDS_SLOT, &SMesh::modifyTexCoords, this);
    newSlot(s_MODIFY_VERTICES_SLOT, &SMesh::modifyVertices, this);

    m_ogreMesh.setNull();
    m_perPrimitiveColorTexture.setNull();

    memset(m_subMeshes, 0, sizeof(m_subMeshes));
}

//-----------------------------------------------------------------------------

SMesh::~SMesh() throw()
{
    if(m_entity)
    {
        ::Ogre::SceneManager* sceneMgr = this->getSceneManager();
        sceneMgr->destroyEntity(m_entity);
    }
}

//-----------------------------------------------------------------------------

void SMesh::doConfigure() throw(fwTools::Failed)
{
    SLM_ASSERT("Not a \"config\" configuration", m_configuration->getName() == "config");

    std::string color = m_configuration->getAttributeValue("color");

    if(m_material)
    {
        m_material->ambient()->setRGBA(color.empty() ? "#ffffffff" : color);
    }

    if(m_configuration->hasAttribute("autoresetcamera"))
    {
        std::string autoResetCamera = m_configuration->getAttributeValue("autoresetcamera");
        m_autoResetCamera = (autoResetCamera == "yes");
    }

    // If a material adaptor is configured in the XML scene, we keep its UID
    // Else we keep the name of the configured Ogre material (if it exists),
    //      it will be passed to the created SMaterial
    if ( m_configuration->hasAttribute("materialAdaptor"))
    {
        m_materialAdaptorUID = m_configuration->getAttributeValue("materialAdaptor");
    }
    else if ( m_configuration->hasAttribute("materialTemplate"))
    {
        // An existing Ogre material will be used for this mesh
        m_materialTemplateName = m_configuration->getAttributeValue("materialTemplate");

        // The mesh adaptor will pass the texture adaptor to the created material adaptor
        if ( m_configuration->hasAttribute("textureAdaptor"))
        {
            m_texAdaptorUID = m_configuration->getAttributeValue("textureAdaptor");
        }
    }

    if(m_configuration->hasAttribute("dynamic"))
    {
        std::string dynamic = m_configuration->getAttributeValue("dynamic");
        m_isDynamic = ( dynamic == "true" );
    }

    if(m_configuration->hasAttribute("dynamicVertices"))
    {
        std::string dynamic = m_configuration->getAttributeValue("dynamicVertices");
        m_isDynamicVertices = ( dynamic == "true" );
    }

    if(m_configuration->hasAttribute("transform"))
    {
        this->setTransformUID(m_configuration->getAttributeValue("transform"));
    }
}

//-----------------------------------------------------------------------------

void SMesh::doStart() throw(::fwTools::Failed)
{
    m_meshName = this->getID() + "_Mesh";

    // Create Mesh Data Structure
    m_ogreMesh = ::Ogre::MeshManager::getSingleton().createManual(
        m_meshName,
        ::Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);

    // We have to create a new material adaptor only if this adaptor is instanciated by a reconstruction adaptor
    // or if no material adaptor uid has been configured
    m_useNewMaterialAdaptor = m_isReconstructionManaged || m_materialAdaptorUID.empty();

    if(!m_useNewMaterialAdaptor)
    {
        // A material adaptor has been configured in the XML scene
        auto materialService = ::fwServices::get(m_materialAdaptorUID);
        m_materialAdaptor = ::visuOgreAdaptor::SMaterial::dynamicCast(materialService);

        m_material = m_materialAdaptor->getObject< ::fwData::Material >();
    }

    ::fwData::Mesh::sptr mesh = this->getObject< ::fwData::Mesh >();
    this->updateMesh(mesh);
}

//-----------------------------------------------------------------------------

void SMesh::doStop() throw(fwTools::Failed)
{
    if(!m_transformService.expired())
    {
        m_transformService.lock()->stop();
        ::fwServices::OSR::unregisterService(m_transformService.lock());
    }

    this->unregisterServices();

    // Destroy Ogre Mesh
    ::Ogre::SceneManager* sceneMgr = this->getSceneManager();
    sceneMgr->destroyManualObject(m_meshName);

    m_connections->disconnect();
    if(!m_useNewMaterialAdaptor)
    {
        m_materialAdaptor.reset();
    }
}

//-----------------------------------------------------------------------------

void SMesh::doSwap() throw(fwTools::Failed)
{
    doStop();
    doStart();
}

//-----------------------------------------------------------------------------

void SMesh::doUpdate() throw(::fwTools::Failed)
{
}

//-----------------------------------------------------------------------------

void SMesh::updateMesh(const ::fwData::Mesh::sptr& mesh)
{
    ::fwComEd::helper::Mesh meshHelper(mesh);

    /// The values in this table refer to vertices in the above table
    size_t uiNumVertices = mesh->getNumberOfPoints();
    OSLM_DEBUG("Vertices #" << uiNumVertices);

    ::Ogre::SceneManager* sceneMgr = this->getSceneManager();

    if(uiNumVertices <= 0)
    {
        SLM_DEBUG("Empty mesh");

        if(m_entity)
        {
            sceneMgr->destroyEntity(m_entity);
            m_entity = nullptr;
        }
        return;
    }

    // Check if mesh attributes
    m_hasNormal = (mesh->getPointNormalsArray() != nullptr);
    m_hasUV     = (mesh->getPointTexCoordsArray() != nullptr);

    this->getRenderService()->makeCurrent();

    //------------------------------------------
    // Create vertex arrays
    //------------------------------------------

    // Create vertex data structure for all vertices shared between submeshes
    if(!m_ogreMesh->sharedVertexData)
    {
        m_ogreMesh->sharedVertexData = new ::Ogre::VertexData();
    }

    ::Ogre::VertexBufferBinding& bind = *m_ogreMesh->sharedVertexData->vertexBufferBinding;
    size_t uiPrevNumVertices = 0;
    if(bind.isBufferBound(POSITION_NORMAL))
    {
        uiPrevNumVertices = bind.getBuffer(POSITION_NORMAL)->getNumVertices();
    }

    if(uiPrevNumVertices < uiNumVertices)
    {
        FW_PROFILE("REALLOC MESH");

        if(!m_hasNormal)
        {
            // Verify if mesh contains Tetra or Edge
            // If not, generate normals
            ::fwData::Mesh::CellTypesMultiArrayType cellsType = meshHelper.getCellTypes();
            bool computeNormals = true;

            ::fwData::mt::ObjectReadLock lock(this->getObject());

            for(unsigned int i = 0; i < cellsType.size() && computeNormals; ++i)
            {
                auto cellType = cellsType[i];
                if(cellType == ::fwData::Mesh::EDGE || cellType == ::fwData::Mesh::TETRA)
                {
                    computeNormals = false;
                }
            }

            if(computeNormals)
            {
                mesh->allocatePointNormals();
                ::fwDataTools::Mesh::generatePointNormals(mesh);
                m_hasNormal = true;
            }
        }

        // We need to reallocate
        m_ogreMesh->sharedVertexData->vertexCount = uiNumVertices;

        // Allocate vertex buffer of the requested number of vertices (vertexCount)
        // and bytes per vertex (offset)
        ::Ogre::HardwareVertexBufferSharedPtr vbuf;
        ::Ogre::HardwareBuffer::Usage usage = (m_isDynamic || m_isDynamicVertices) ?
                                              ::Ogre::HardwareBuffer::HBU_DYNAMIC_WRITE_ONLY_DISCARDABLE :
                                              ::Ogre::HardwareBuffer::HBU_STATIC_WRITE_ONLY;

        ::Ogre::HardwareBufferManager& mgr = ::Ogre::HardwareBufferManager::getSingleton();

        size_t offsetMain = 0;

        // Create declaration (memory format) of vertex data based on ::fwData::Mesh
        ::Ogre::VertexDeclaration* declMain = m_ogreMesh->sharedVertexData->vertexDeclaration;

        // Clear if necessary
        declMain->removeAllElements();
        // 1st buffer
        declMain->addElement(POSITION_NORMAL, offsetMain, ::Ogre::VET_FLOAT3, ::Ogre::VES_POSITION);
        offsetMain += ::Ogre::VertexElement::getTypeSize(Ogre::VET_FLOAT3);

        if(m_hasNormal)
        {
            declMain->addElement(POSITION_NORMAL, offsetMain, ::Ogre::VET_FLOAT3, ::Ogre::VES_NORMAL);
            offsetMain += ::Ogre::VertexElement::getTypeSize(Ogre::VET_FLOAT3);
        }

        // Set vertex buffer binding so buffer 0 is bound to our vertex buffer
        vbuf = mgr.createVertexBuffer(offsetMain, uiNumVertices, usage, false);
        bind.setBinding(POSITION_NORMAL, vbuf);

        // Allocate other buffers


        // . UV Buffer - By now, we just use one UV coordinates set for each mesh
        if (m_hasUV)
        {
            ::Ogre::VertexDeclaration* declUV = m_ogreMesh->sharedVertexData->vertexDeclaration;
            size_t offsetUV = 0;
            declUV->addElement(TEXCOORD, offsetUV, ::Ogre::VET_FLOAT2, ::Ogre::VES_TEXTURE_COORDINATES);
            offsetUV += ::Ogre::VertexElement::getTypeSize(Ogre::VET_FLOAT2);

            // Allocate vertex buffer of the requested number of vertices (vertexCount)
            // and bytes per vertex (offset)
            ::Ogre::HardwareVertexBufferSharedPtr uvbuf = mgr.createVertexBuffer(offsetUV, uiNumVertices, usage, false);
            bind.setBinding(TEXCOORD, uvbuf);
        }

    }
    else
    {
        // We don't reallocate, we keep the same vertex buffers and only update the number of vertices
        m_ogreMesh->sharedVertexData->vertexCount = uiNumVertices;
    }


    //------------------------------------------
    // Create indices arrays
    //------------------------------------------

    // 1 - Count number of primitives for each type
    ::fwData::Mesh::CellTypesMultiArrayType cellsType = meshHelper.getCellTypes();
    unsigned int numIndices[ s_numPrimitiveTypes ];

    memset(numIndices, 0, sizeof(numIndices));

    ::fwData::mt::ObjectReadLock lock(this->getObject());

    for(unsigned int i = 0; i < cellsType.size(); ++i)
    {
        auto cellType = cellsType[i];
        if(cellType == ::fwData::Mesh::TRIANGLE)
        {
            numIndices[::fwData::Mesh::TRIANGLE] += 3;
        }
        else if(cellType == ::fwData::Mesh::QUAD)
        {
            numIndices[::fwData::Mesh::QUAD] += 6;
        }
        else if(cellType == ::fwData::Mesh::EDGE)
        {
            numIndices[::fwData::Mesh::EDGE] += 2;
        }
        else if(cellType == ::fwData::Mesh::TETRA)
        {
            numIndices[::fwData::Mesh::TETRA] += 12;
        }
        else
        {
            OSLM_ERROR("Unhandled cell type in Ogre mesh: " << static_cast<int>(cellType));
        }
    }

    // 2 - Create a submesh for each primitive type
    void* indexBuffer[ s_numPrimitiveTypes ];
    memset(indexBuffer, 0, sizeof(indexBuffer));

    const bool indices32Bits     = uiNumVertices >= (1 << 16);
    const bool indicesPrev32Bits = uiPrevNumVertices >= (1 << 16);

    {
        FW_PROFILE_AVG("REALLOC INDEX", 5);

        for(size_t i = 0; i < s_numPrimitiveTypes; ++i)
        {
            ::fwData::Mesh::CellTypesEnum cellType = static_cast< ::fwData::Mesh::CellTypesEnum>(i);

            if(numIndices[i] > 0)
            {
                if(!m_subMeshes[i])
                {
                    // Create one submesh
                    std::string name = std::to_string(cellType);
                    m_subMeshes[i] = m_ogreMesh->createSubMesh(name);
                    if ( cellType == ::fwData::Mesh::TRIANGLE || cellType == ::fwData::Mesh::QUAD )
                    {
                        m_subMeshes[i]->operationType = ::Ogre::RenderOperation::OT_TRIANGLE_LIST;
                    }
                    else if(cellType == ::fwData::Mesh::TETRA || cellType == ::fwData::Mesh::EDGE)
                    {
                        m_subMeshes[i]->operationType = ::Ogre::RenderOperation::OT_LINE_LIST;
                    }

                    m_subMeshes[i]->useSharedVertices     = true;
                    m_subMeshes[i]->indexData->indexStart = 0;
                }

                ::Ogre::HardwareIndexBufferSharedPtr ibuf = m_subMeshes[i]->indexData->indexBuffer;

                // Allocate index buffer of the requested number of vertices (ibufCount) if necessary
                // We don't reallocate if we have more space than requested
                bool createIndexBuffer = ibuf.isNull();
                if( !ibuf.isNull())
                {
                    // realocate if new mesh has more indexes or IndexType change
                    createIndexBuffer = (ibuf->getNumIndexes() < numIndices[i]) || (indicesPrev32Bits != indices32Bits);
                }
                if(createIndexBuffer)
                {
                    ::Ogre::HardwareBuffer::Usage usage = m_isDynamic ?
                                                          ::Ogre::HardwareBuffer::HBU_DYNAMIC_WRITE_ONLY_DISCARDABLE :
                                                          ::Ogre::HardwareBuffer::HBU_STATIC_WRITE_ONLY;

                    ibuf = ::Ogre::HardwareBufferManager::getSingleton().createIndexBuffer(
                        indices32Bits ? ::Ogre::HardwareIndexBuffer::IT_32BIT : ::Ogre::HardwareIndexBuffer::IT_16BIT,
                        numIndices[i], usage);

                    m_subMeshes[i]->indexData->indexBuffer = ibuf;
                }
                m_subMeshes[i]->indexData->indexCount = numIndices[i];
                OSLM_DEBUG("Index #" << m_subMeshes[i]->indexData->indexCount );

                // Lock index data ow, we are going to write into it in the next loop
                indexBuffer[i] = ibuf->lock(Ogre::HardwareBuffer::HBL_DISCARD);
            }
            else
            {
                // Destroy the submesh if it has been created before - a submesh with 0 index would be invalid
                if(m_subMeshes[i])
                {
                    std::string name = std::to_string(cellType);
                    m_ogreMesh->destroySubMesh(name);
                    m_subMeshes[i] = nullptr;
                }
            }
        }
    }

    // 3 - Copy indices data

    if(indices32Bits)
    {
        copyIndices< std::uint32_t >( indexBuffer[::fwData::Mesh::TRIANGLE],
                                      indexBuffer[::fwData::Mesh::QUAD],
                                      indexBuffer[::fwData::Mesh::EDGE],
                                      indexBuffer[::fwData::Mesh::TETRA],
                                      meshHelper, mesh->getNumberOfCells() );
    }
    else
    {
        copyIndices< std::uint16_t >( indexBuffer[::fwData::Mesh::TRIANGLE],
                                      indexBuffer[::fwData::Mesh::QUAD],
                                      indexBuffer[::fwData::Mesh::EDGE],
                                      indexBuffer[::fwData::Mesh::TETRA],
                                      meshHelper, mesh->getNumberOfCells() );
    }

    for(size_t i = 0; i < s_numPrimitiveTypes; ++i)
    {
        if(numIndices[i] > 0)
        {
            m_subMeshes[i]->indexData->indexBuffer->unlock();
        }
    }

    lock.unlock();

    // Add mesh to Ogre Root Scene Node if it doesn't exist yet
    if(!m_entity)
    {
        m_entity = sceneMgr->createEntity(m_ogreMesh);
    }

    this->createTransformService();
    if(m_useNewMaterialAdaptor)
    {
        this->updateNewMaterialAdaptor();
    }
    else
    {
        this->updateXMLMaterialAdaptor();
    }

    // Update vertex attributes
    this->updateVertices(mesh);
    this->updateColors(mesh);
    this->updateTexCoords(mesh);

    if (m_autoResetCamera)
    {
        this->getRenderService()->resetCameraCoordinates(m_layerID);
    }
}

//-----------------------------------------------------------------------------

void SMesh::updateVertices(const ::fwData::Mesh::sptr& mesh)
{
    SLM_ASSERT("Nonexistent ::Ogre::Entity", m_entity);
    FW_PROFILE_AVG("UPDATE VERTICES", 5);

    // Getting Vertex Buffer
    ::Ogre::VertexBufferBinding* bind          = m_ogreMesh->sharedVertexData->vertexBufferBinding;
    ::Ogre::HardwareVertexBufferSharedPtr vbuf = bind->getBuffer(POSITION_NORMAL);

    /// Upload the index data to the card
    void* pVertex = vbuf->lock(Ogre::HardwareBuffer::HBL_DISCARD);

    // Update Ogre Mesh with ::fwData::Mesh
    ::fwData::mt::ObjectReadLock lock(mesh);
    ::fwComEd::helper::Mesh meshHelper(mesh);
    ::fwData::Mesh::PointsMultiArrayType points = meshHelper.getPoints();

    size_t uiStrideFloat = 3;
    if(m_hasNormal)
    {
        uiStrideFloat += 3;
    }

    typedef ::fwData::Mesh::PointValueType PointValueType;
    typedef ::fwData::Mesh::NormalValueType NormalValueType;

    // Copy position and normal of each vertices
    // Compute bounding box (for culling)
    float xMin = std::numeric_limits<float>::max();
    float yMin = std::numeric_limits<float>::max();
    float zMin = std::numeric_limits<float>::max();
    float xMax = -std::numeric_limits<float>::max();
    float yMax = -std::numeric_limits<float>::max();
    float zMax = -std::numeric_limits<float>::max();

    {
        FW_PROFILE_AVG("UPDATE BBOX", 5);
        for (unsigned int i = 0; i < mesh->getNumberOfPoints(); ++i)
        {
            const auto& pt0 = points[i][0];
            xMin = std::min(xMin, pt0);
            xMax = std::max(xMax, pt0);

            const auto& pt1 = points[i][1];
            yMin = std::min(yMin, pt1);
            yMax = std::max(yMax, pt1);

            const auto& pt2 = points[i][2];
            zMin = std::min(zMin, pt2);
            zMax = std::max(zMax, pt2);
        }
    }
    {
        PointValueType* __restrict pPos = static_cast< PointValueType* >( pVertex );
        FW_PROFILE_AVG("UPDATE POS", 5);
        for (unsigned int i = 0; i < mesh->getNumberOfPoints(); ++i)
        {
            memcpy(pPos, &points[i][0], 3 * sizeof(PointValueType) );

            pPos += uiStrideFloat;
        }
    }

    if(m_hasNormal)
    {
        FW_PROFILE_AVG("UPDATE NORMALS", 5);
        ::fwData::Mesh::PointNormalsMultiArrayType normals = meshHelper.getPointNormals();
        NormalValueType* __restrict pNormal = static_cast< NormalValueType* >( pVertex ) + 3;

        for (unsigned int i = 0; i < mesh->getNumberOfPoints(); ++i)
        {
            memcpy(pNormal, &normals[i][0], 3 * sizeof(NormalValueType) );
            pNormal += uiStrideFloat;
        }
    }

    // Unlock vertex data
    vbuf->unlock();
    lock.unlock();

    m_ogreMesh->_setBounds( ::Ogre::AxisAlignedBox( xMin, yMin, zMin, xMax, yMax, zMax) );
    m_ogreMesh->_setBoundingSphereRadius( ::Ogre::Math::Sqrt(   ::Ogre::Math::Sqr(xMax - xMin) +
                                                                ::Ogre::Math::Sqr(yMax - yMin) +
                                                                ::Ogre::Math::Sqr(zMax - zMin)) /2);

}

//-----------------------------------------------------------------------------

void SMesh::updateColors(const ::fwData::Mesh::sptr& mesh)
{
    SLM_ASSERT("Nonexistent ::Ogre::Entity", m_entity);

    FW_PROFILE_AVG("UPDATE COLORS", 5);

    const bool hasVertexColor    = (mesh->getPointColorsArray() != nullptr);
    const bool hasPrimitiveColor = (mesh->getCellColorsArray() != nullptr);

    OSLM_ERROR_IF("Mesh " << mesh->getID() << " contains both per-cell and per-vertex color.\n"
                  "Per-vertex color will be used.", hasVertexColor && hasPrimitiveColor);


    ::Ogre::VertexBufferBinding* bind = m_ogreMesh->sharedVertexData->vertexBufferBinding;
    SLM_ASSERT("Invalid vertex buffer binding", bind);

    // 1 - Initialization
    if(hasVertexColor)
    {
        size_t offsetColor = 0;
        ::Ogre::VertexDeclaration* vtxDecl = m_ogreMesh->sharedVertexData->vertexDeclaration;

        if(!vtxDecl->findElementBySemantic(::Ogre::VES_DIFFUSE))
        {
            vtxDecl->addElement(COLOUR, offsetColor, ::Ogre::VET_COLOUR, ::Ogre::VES_DIFFUSE);
            offsetColor += ::Ogre::VertexElement::getTypeSize(Ogre::VET_COLOUR);
        }

        ::Ogre::HardwareVertexBufferSharedPtr cbuf;

        size_t uiNumVertices     = mesh->getNumberOfPoints();
        size_t uiPrevNumVertices = 0;
        if(bind->isBufferBound(COLOUR))
        {
            cbuf              = bind->getBuffer(COLOUR);
            uiPrevNumVertices = cbuf->getNumVertices();
        }

        if(!bind->isBufferBound(COLOUR) || uiPrevNumVertices < uiNumVertices )
        {
            FW_PROFILE_AVG("REALLOC COLORS_VERTEX", 5);

            // Allocate color buffer of the requested number of vertices (vertexCount) and bytes per vertex (offset)
            ::Ogre::HardwareBuffer::Usage usage = (m_isDynamic || m_isDynamicVertices) ?
                                                  ::Ogre::HardwareBuffer::HBU_DYNAMIC_WRITE_ONLY_DISCARDABLE :
                                                  ::Ogre::HardwareBuffer::HBU_STATIC_WRITE_ONLY;


            ::Ogre::HardwareBufferManager& mgr = ::Ogre::HardwareBufferManager::getSingleton();

            cbuf = mgr.createVertexBuffer(offsetColor, uiNumVertices, usage, false);
            bind->setBinding(COLOUR, cbuf);

            m_perPrimitiveColorTexture.setNull();
            m_perPrimitiveColorTextureName = "";
        }
    }
    else if(hasPrimitiveColor)
    {
        unsigned int numIndicesTotal = 0;
        for(size_t i = 0; i < s_numPrimitiveTypes; ++i)
        {
            if(m_subMeshes[i])
            {
                numIndicesTotal += m_subMeshes[i]->indexData->indexCount;
            }
        }

        if(m_perPrimitiveColorTexture.isNull())
        {
            m_perPrimitiveColorTextureName = this->getID() + "_PerCellColorTexture";
            m_perPrimitiveColorTexture     = ::Ogre::TextureManager::getSingleton().create(
                m_perPrimitiveColorTextureName, ::Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME, true);
        }

        size_t width  = std::min(s_maxTextureSize, numIndicesTotal);
        size_t height = std::ceil(numIndicesTotal / s_maxTextureSize);

        if(m_perPrimitiveColorTexture->getWidth() != width || m_perPrimitiveColorTexture->getHeight() != height )
        {
            FW_PROFILE_AVG("REALLOC COLORS_CELL", 5);

            auto usage = (m_isDynamic || m_isDynamicVertices) ?
                         ::Ogre::TU_DYNAMIC_WRITE_ONLY_DISCARDABLE :
                         ::Ogre::TU_STATIC_WRITE_ONLY;

            m_perPrimitiveColorTexture->freeInternalResources();

            m_perPrimitiveColorTexture->setWidth(static_cast< ::Ogre::uint32>(width));
            m_perPrimitiveColorTexture->setHeight(static_cast< ::Ogre::uint32>(height));
            m_perPrimitiveColorTexture->setDepth(static_cast< ::Ogre::uint32>(1));
            m_perPrimitiveColorTexture->setTextureType(::Ogre::TEX_TYPE_2D);
            m_perPrimitiveColorTexture->setNumMipmaps(0);
            m_perPrimitiveColorTexture->setFormat(::Ogre::PF_BYTE_RGBA);
            m_perPrimitiveColorTexture->setUsage(usage);

            m_perPrimitiveColorTexture->createInternalResources();

            // Unbind vertex color if it was previously enabled
            if(bind->isBufferBound(COLOUR))
            {
                bind->unsetBinding(COLOUR);
            }
        }
    }

    ::Ogre::RenderSystem* rs = ::Ogre::Root::getSingleton().getRenderSystem();

    if (hasVertexColor)
    {
        ::fwData::mt::ObjectReadLock lock(mesh);
        ::fwComEd::helper::Mesh meshHelper(mesh);

        ::Ogre::HardwareVertexBufferSharedPtr cbuf = bind->getBuffer(COLOUR);
        void* pCol = cbuf->lock(Ogre::HardwareBuffer::HBL_DISCARD);
        ::Ogre::RGBA* pColor = static_cast< ::Ogre::RGBA* >( pCol );

        auto colors          = meshHelper.getPointColors();
        size_t numComponents = colors.shape()[1];

        if(numComponents == 3)
        {
            for (unsigned int i = 0; i < mesh->getNumberOfPoints(); ++i)
            {
                rs->convertColourValue(::Ogre::ColourValue(colors[i][0] / 255.f, colors[i][1] / 255.f,
                                                           colors[i][2] / 255.f), pColor++);
            }
        }
        else if(numComponents == 4)
        {
            for (unsigned int i = 0; i < mesh->getNumberOfPoints(); ++i)
            {
                rs->convertColourValue(::Ogre::ColourValue(colors[i][0] / 255.f, colors[i][1] / 255.f,
                                                           colors[i][2] / 255.f, colors[i][3] / 255.f), pColor++);
            }
        }
        else
        {
            SLM_FATAL("We only support RGB or RGBA vertex color");
        }

        cbuf->unlock();
    }
    else if(hasPrimitiveColor)
    {
        ::fwData::mt::ObjectReadLock lock(mesh);
        ::fwComEd::helper::Mesh meshHelper(mesh);

        ::Ogre::HardwarePixelBufferSharedPtr pixelBuffer = m_perPrimitiveColorTexture->getBuffer();

        pixelBuffer->lock(::Ogre::HardwareBuffer::HBL_DISCARD);
        const ::Ogre::PixelBox& pixelBox = pixelBuffer->getCurrentLock();

        unsigned int* __restrict pColorDest = static_cast<unsigned int*>(pixelBox.data);

        auto colors          = meshHelper.getCellColors();
        auto cellsType       = meshHelper.getCellTypes();
        size_t numComponents = colors.shape()[1];

        if(numComponents == 3)
        {
            const auto uiNumCells = mesh->getNumberOfCells();
            for (unsigned int i = 0; i < uiNumCells; ++i)
            {
                if ( cellsType[i] == ::fwData::Mesh::TRIANGLE ||
                     cellsType[i] == ::fwData::Mesh::EDGE)
                {
                    const auto color = colors[i];

                    rs->convertColourValue(::Ogre::ColourValue(color[0] / 255.f, color[1] / 255.f, color[2] / 255.f),
                                           pColorDest++);
                }
                else if ( cellsType[i] == ::fwData::Mesh::QUAD )
                {
                    SLM_ERROR("Not yet implemented");
                }
                else if ( cellsType[i] == ::fwData::Mesh::TETRA )
                {
                    SLM_ERROR("Not yet implemented");
                }
            }
        }
        else if(numComponents == 4)
        {
            const auto uiNumCells = mesh->getNumberOfCells();
            for (unsigned int i = 0; i < uiNumCells; ++i)
            {
                if ( cellsType[i] == ::fwData::Mesh::TRIANGLE ||
                     cellsType[i] == ::fwData::Mesh::EDGE)
                {
                    const auto color = colors[i];

                    rs->convertColourValue(::Ogre::ColourValue(color[0] / 255.f, color[1] / 255.f, color[2] / 255.f,
                                                               color[3] / 255.f),
                                           pColorDest++);
                }
                else if ( cellsType[i] == ::fwData::Mesh::QUAD )
                {
                    SLM_ERROR("Not yet implemented");
                }
                else if ( cellsType[i] == ::fwData::Mesh::TETRA )
                {
                    SLM_ERROR("Not yet implemented");
                }
            }
        }
        else
        {
            SLM_FATAL("We only support RGB or RGBA vertex color");
        }


        pixelBuffer->unlock();
    }

    if(hasVertexColor != m_hasVertexColor || hasPrimitiveColor != m_hasPrimitiveColor)
    {
        m_materialAdaptor->setHasVertexColor(hasVertexColor);
        m_materialAdaptor->setHasPrimitiveColor(hasPrimitiveColor, m_perPrimitiveColorTextureName);
        m_materialAdaptor->slot(::visuOgreAdaptor::SMaterial::s_UPDATE_SLOT)->asyncRun();

        m_hasVertexColor    = hasVertexColor;
        m_hasPrimitiveColor = hasPrimitiveColor;
    }
}

//-----------------------------------------------------------------------------

void SMesh::updateTexCoords(const ::fwData::Mesh::sptr& mesh)
{
    m_hasUV = (mesh->getPointTexCoordsArray() != nullptr);

    if (m_hasUV)
    {
        FW_PROFILE_AVG("UPDATE TexCoords", 5);

        ::fwData::mt::ObjectReadLock lock(mesh);
        ::fwComEd::helper::Mesh meshHelper(mesh);

        ::Ogre::VertexBufferBinding* bind           = m_ogreMesh->sharedVertexData->vertexBufferBinding;
        ::Ogre::HardwareVertexBufferSharedPtr uvbuf = bind->getBuffer(TEXCOORD);
        void* pBuf = uvbuf->lock(Ogre::HardwareBuffer::HBL_DISCARD);
        float* pUV = static_cast< float* >( pBuf );

        ::fwData::Mesh::PointTexCoordsMultiArrayType uvCoord = meshHelper.getPointTexCoords();

        // Copy UV coordinates for each mesh point
        for (unsigned int i = 0; i < mesh->getNumberOfPoints(); ++i)
        {
            pUV[2 * i]     = uvCoord[i][0];
            pUV[2 * i + 1] = uvCoord[i][1];
        }

        uvbuf->unlock();
    }
}

//------------------------------------------------------------------------------

void SMesh::updateNewMaterialAdaptor()
{
    if(!m_materialAdaptor)
    {
        if(m_entity)
        {
            auto materialService =
                ::fwServices::add< ::fwRenderOgre::IAdaptor >(m_material, "::visuOgreAdaptor::SMaterial");
            SLM_ASSERT("Material service not instantiated", materialService);
            m_materialAdaptor = ::visuOgreAdaptor::SMaterial::dynamicCast(materialService);

            m_materialAdaptor->setID(this->getID() + "_" + m_materialAdaptor->getID());
            m_materialAdaptor->setRenderService( this->getRenderService() );
            m_materialAdaptor->setLayerID(m_layerID);

            if (!m_materialTemplateName.empty())
            {
                m_materialAdaptor->setMaterialTemplateName(m_materialTemplateName);
            }

            std::string meshName = this->getObject()->getID();
            m_materialAdaptor->setMaterialName(meshName + "_" + m_materialAdaptor->getID() + "_Material");
            m_materialAdaptor->setHasMeshNormal(m_hasNormal);
            m_materialAdaptor->setHasVertexColor(m_hasVertexColor);
            m_materialAdaptor->setHasPrimitiveColor(m_hasPrimitiveColor, m_perPrimitiveColorTextureName);

            m_materialAdaptor->start();

            if (!m_texAdaptorUID.empty())
            {
                m_materialAdaptor->setTextureAdaptor(m_texAdaptorUID);
            }

            m_materialAdaptor->update();
            this->registerService(m_materialAdaptor);

            m_entity->setMaterialName(m_materialAdaptor->getMaterialName());
        }
    }
    else if(m_materialAdaptor->getObject< ::fwData::Material >() != m_material)
    {
        m_materialAdaptor->swap(m_material);
    }
}

//------------------------------------------------------------------------------

void SMesh::updateXMLMaterialAdaptor()
{
    if(m_materialAdaptor->getUpdatingStatus() == UpdatingStatus::NOTUPDATING)
    {
        if(m_materialAdaptor->getMaterialName().empty())
        {
            std::string meshName = this->getObject()->getID();
            m_materialAdaptor->setMaterialName(meshName + "_Material");
        }

        m_materialAdaptor->setHasMeshNormal(m_hasNormal);
        m_materialAdaptor->setHasVertexColor(m_hasVertexColor);
        m_materialAdaptor->setHasPrimitiveColor(m_hasPrimitiveColor, m_perPrimitiveColorTextureName);

        if(m_entity)
        {
            m_entity->setMaterialName(m_materialAdaptor->getMaterialName());
        }
    }
    else if(m_materialAdaptor->getObject< ::fwData::Material >() != m_material)
    {
        m_materialAdaptor->swap(m_material);
    }
}

//------------------------------------------------------------------------------

void SMesh::createTransformService()
{
    // We need to create a transform service only one time
    if(m_transformService.expired())
    {
        ::fwData::Mesh::sptr mesh = this->getObject < ::fwData::Mesh >();

        ::fwData::TransformationMatrix3D::sptr fieldTransform;

        // Get existing TransformationMatrix3D, else create an empty one
        if(!this->getTransformUID().empty())
        {
            auto object = ::fwTools::fwID::getObject(this->getTransformUID());
            if(!object)
            {
                fieldTransform = ::fwData::TransformationMatrix3D::New();
            }
            else
            {
                fieldTransform = ::fwData::TransformationMatrix3D::dynamicCast(object);
                OSLM_ASSERT("Object is not a transform", fieldTransform);
            }
        }
        else
        {
            fieldTransform = ::fwData::TransformationMatrix3D::New();
        }

        // Try to set fieldTransform as default transform of the mesh
        mesh->setField("TransformMatrix", fieldTransform);

        m_transformService = ::fwServices::add< ::fwRenderOgre::IAdaptor >(fieldTransform,
                                                                           "::visuOgreAdaptor::STransform");
        SLM_ASSERT("Transform service is null", m_transformService.lock());
        ::visuOgreAdaptor::STransform::sptr transformService = ::visuOgreAdaptor::STransform::dynamicCast(
            m_transformService.lock());

        transformService->setID(this->getID() + "_" + transformService->getID());
        transformService->setRenderService ( this->getRenderService() );
        transformService->setLayerID(m_layerID);
        transformService->setTransformUID(this->getTransformUID());
        transformService->setParentTransformUID(this->getParentTransformUID());

        transformService->start();
        this->registerService(transformService);

        // Normally updated by updateFromF4S right?
        ::Ogre::SceneNode* transNode = transformService->getSceneNode();

        ::Ogre::SceneNode* node = m_entity->getParentSceneNode();
        if ((node != transNode) && transNode)
        {
            m_entity->detachFromParent();
            transNode->attachObject(m_entity);
        }
    }
}

//-----------------------------------------------------------------------------

void SMesh::modifyMesh()
{
    if((m_isDynamic || m_isDynamicVertices) && (!getVisibility() || !this->getRenderService()->isShownOnScreen()))
    {
        return;
    }
    ::fwData::Mesh::sptr mesh = this->getObject< ::fwData::Mesh >();
    this->updateMesh(mesh);
}

//-----------------------------------------------------------------------------

void SMesh::modifyPointColors()
{
    if((m_isDynamic || m_isDynamicVertices) && (!getVisibility() || !this->getRenderService()->isShownOnScreen()))
    {
        return;
    }

    // Keep the make current outside to avoid too many context changes when we update multiple attributes
    this->getRenderService()->makeCurrent();

    ::fwData::Mesh::sptr mesh = this->getObject< ::fwData::Mesh >();
    this->updateColors(mesh);

    /// Notify -Mesh object that it has been loaded
    m_ogreMesh->load();
    this->requestRender();
}

//-----------------------------------------------------------------------------

void SMesh::modifyTexCoords()
{
    if((m_isDynamic || m_isDynamicVertices) && (!getVisibility() || !this->getRenderService()->isShownOnScreen()))
    {
        return;
    }

    // Keep the make current outside to avoid too many context changes when we update multiple attributes
    this->getRenderService()->makeCurrent();

    ::fwData::Mesh::sptr mesh = this->getObject< ::fwData::Mesh >();
    this->updateTexCoords(mesh);

    /// Notify -Mesh object that it has been loaded
    m_ogreMesh->load();
    this->requestRender();
}

//-----------------------------------------------------------------------------

void SMesh::modifyVertices()
{
    if((m_isDynamic || m_isDynamicVertices) && (!getVisibility() || !this->getRenderService()->isShownOnScreen()))
    {
        return;
    }

    // Keep the make current outside to avoid too many context changes when we update multiple attributes
    this->getRenderService()->makeCurrent();

    ::fwData::Mesh::sptr mesh = this->getObject< ::fwData::Mesh >();
    this->updateVertices(mesh);

    /// Notify -Mesh object that it has been loaded
    m_ogreMesh->load();
    this->requestRender();
}

//-----------------------------------------------------------------------------

::fwServices::IService::KeyConnectionsType SMesh::getObjSrvConnections() const
{
    ::fwServices::IService::KeyConnectionsType connections;
    connections.push_back( std::make_pair( ::fwData::Mesh::s_VERTEX_MODIFIED_SIG, s_MODIFY_VERTICES_SLOT ) );
    connections.push_back( std::make_pair( ::fwData::Mesh::s_POINT_COLORS_MODIFIED_SIG, s_MODIFY_COLORS_SLOT ) );
    connections.push_back( std::make_pair( ::fwData::Mesh::s_CELL_COLORS_MODIFIED_SIG, s_MODIFY_COLORS_SLOT ) );
    connections.push_back( std::make_pair( ::fwData::Mesh::s_POINT_TEX_COORDS_MODIFIED_SIG,
                                           s_MODIFY_POINT_TEX_COORDS_SLOT ) );
    connections.push_back( std::make_pair( ::fwData::Mesh::s_MODIFIED_SIG, s_MODIFY_MESH_SLOT ) );
    return connections;
}

//-----------------------------------------------------------------------------

} //namespace visuOgreAdaptor
