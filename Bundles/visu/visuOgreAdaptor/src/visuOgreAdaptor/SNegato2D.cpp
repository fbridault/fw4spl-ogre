/* ***** BEGIN LICENSE BLOCK *****
 * FW4SPL - Copyright (C) IRCAD, 2014-2018.
 * Distributed under the terms of the GNU Lesser General Public License (LGPL) as
 * published by the Free Software Foundation.
 * ****** END LICENSE BLOCK ****** */

#include "visuOgreAdaptor/SNegato2D.hpp"

#include <fwCom/Signal.hxx>
#include <fwCom/Slot.hxx>
#include <fwCom/Slots.hxx>

#include <fwData/Image.hpp>

#include <fwDataTools/fieldHelper/MedicalImageHelpers.hpp>

#include <fwRenderOgre/Utils.hpp>

#include <fwServices/macros.hpp>

#include <OgreCamera.h>
#include <OgreSceneNode.h>
#include <OgreTextureManager.h>

#include <algorithm>

namespace visuOgreAdaptor
{

fwServicesRegisterMacro( ::fwRenderOgre::IAdaptor, ::visuOgreAdaptor::SNegato2D, ::fwData::Image);

const ::fwCom::Slots::SlotKeyType s_NEWIMAGE_SLOT   = "newImage";
const ::fwCom::Slots::SlotKeyType s_SLICETYPE_SLOT  = "sliceType";
const ::fwCom::Slots::SlotKeyType s_SLICEINDEX_SLOT = "sliceIndex";

static const ::fwServices::IService::KeyType s_IMAGE_INOUT = "image";
static const ::fwServices::IService::KeyType s_TF_INOUT    = "tf";

static const std::string s_ENABLE_APLHA_CONFIG = "tfalpha";

//------------------------------------------------------------------------------

SNegato2D::SNegato2D() noexcept :
    ::fwDataTools::helper::MedicalImageAdaptor(),
    m_plane(nullptr),
    m_negatoSceneNode(nullptr),
    m_filtering( ::fwRenderOgre::Plane::FilteringEnumType::NONE )
{
    this->installTFSlots(this);

    m_currentSliceIndex = {0.f, 0.f, 0.f};

    newSlot(s_NEWIMAGE_SLOT, &SNegato2D::newImage, this);
    newSlot(s_SLICETYPE_SLOT, &SNegato2D::changeSliceType, this);
    newSlot(s_SLICEINDEX_SLOT, &SNegato2D::changeSliceIndex, this);
}

//------------------------------------------------------------------------------

SNegato2D::~SNegato2D() noexcept
{
}

//------------------------------------------------------------------------------

void SNegato2D::configuring()
{
    this->configureParams();

    const ConfigType config = this->getConfigTree().get_child("config.<xmlattr>");

    if(config.count("sliceIndex"))
    {
        const std::string orientation = config.get<std::string>("sliceIndex");

        if(orientation == "axial")
        {
            m_orientation = Z_AXIS;
        }
        else if(orientation == "frontal")
        {
            m_orientation = Y_AXIS;
        }
        else if(orientation == "sagittal")
        {
            m_orientation = X_AXIS;
        }
    }
    else
    {
        // Axis orientation mode by default
        m_orientation = OrientationMode::Z_AXIS;
    }

    if(config.count("filtering"))
    {
        const std::string filteringValue = config.get<std::string>("filtering");
        ::fwRenderOgre::Plane::FilteringEnumType filtering(::fwRenderOgre::Plane::FilteringEnumType::LINEAR);

        if(filteringValue == "none")
        {
            filtering = ::fwRenderOgre::Plane::FilteringEnumType::NONE;
        }
        else if(filteringValue == "anisotropic")
        {
            filtering = ::fwRenderOgre::Plane::FilteringEnumType::ANISOTROPIC;
        }

        this->setFiltering(filtering);
    }

    m_enableAlpha = config.get<bool>(s_ENABLE_APLHA_CONFIG, m_enableAlpha);
}

//------------------------------------------------------------------------------

void SNegato2D::starting()
{
    this->initialize();

    ::fwData::Image::sptr image = this->getInOut< ::fwData::Image >(s_IMAGE_INOUT);
    SLM_ASSERT("inout '" + s_IMAGE_INOUT + "' is missing.", image);

    ::fwData::TransferFunction::sptr tf = this->getInOut< ::fwData::TransferFunction >(s_TF_INOUT);
    this->setOrCreateTF(tf, image);

    this->updateImageInfos(this->getInOut< ::fwData::Image >(s_IMAGE_INOUT));

    // 3D source texture instantiation
    m_3DOgreTexture = ::Ogre::TextureManager::getSingleton().create(
        this->getID() + "_Texture",
        ::Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
        true);

    // TF texture initialization
    m_gpuTF = std::unique_ptr< ::fwRenderOgre::TransferFunction>(new ::fwRenderOgre::TransferFunction());
    m_gpuTF->createTexture(this->getID());

    // Scene node's instanciation
    m_negatoSceneNode = this->getSceneManager()->getRootSceneNode()->createChildSceneNode();

    // Plane's instanciation
    m_plane = new ::fwRenderOgre::Plane(this->getID(), m_negatoSceneNode, getSceneManager(),
                                        OrientationMode::X_AXIS, false, m_3DOgreTexture, m_filtering);

    this->getLayer()->getDefaultCamera()->setProjectionType( ::Ogre::ProjectionType::PT_ORTHOGRAPHIC );

    bool isValid = ::fwDataTools::fieldHelper::MedicalImageHelpers::checkImageValidity(image);
    if(isValid)
    {
        this->newImage();
    }
}

//------------------------------------------------------------------------------

void SNegato2D::stopping()
{
    this->removeTFConnections();

    if (!m_connection.expired())
    {
        m_connection.disconnect();
    }

    delete m_plane;

    m_3DOgreTexture.reset();
    m_gpuTF.reset();

    this->requestRender();
}

//------------------------------------------------------------------------------

void SNegato2D::updating()
{
    this->requestRender();
}

//------------------------------------------------------------------------------

void SNegato2D::swapping(const KeyType& key)
{
    if (key == s_TF_INOUT)
    {
        ::fwData::TransferFunction::sptr tf = this->getInOut< ::fwData::TransferFunction >(s_TF_INOUT);
        ::fwData::Image::sptr image         = this->getInOut< ::fwData::Image >(s_IMAGE_INOUT);
        SLM_ASSERT("Missing image", image);

        this->setOrCreateTF(tf, image);
        this->updateTFPoints();
    }
}

//------------------------------------------------------------------------------

void SNegato2D::newImage()
{
    if(!m_3DOgreTexture)
    {
        // The adaptor hasn't start yet (the window is maybe not visible)
        return;
    }
    this->getRenderService()->makeCurrent();

    ::fwData::Image::sptr image = this->getInOut< ::fwData::Image >(s_IMAGE_INOUT);
    SLM_ASSERT("inout '" + s_IMAGE_INOUT + "' is missing", image);

    // Retrieves or creates the slice index fields
    this->updateImageInfos(image);

    ::fwRenderOgre::Utils::convertImageForNegato(m_3DOgreTexture.get(), image);

    this->createPlane( image->getSpacing() );
    this->updateCameraWindowBounds();

    // Update Slice
    this->changeSliceIndex(m_axialIndex->value(), m_frontalIndex->value(), m_sagittalIndex->value());

    // Update tranfer function in Gpu programs
    this->updateTFPoints();

    this->requestRender();
}

//------------------------------------------------------------------------------

void SNegato2D::changeSliceType(int /*_from*/, int _to)
{
    OrientationMode newOrientationMode = OrientationMode::X_AXIS;

    switch (_to)
    {
        case 0:
            newOrientationMode = OrientationMode::X_AXIS;
            break;
        case 1:
            newOrientationMode = OrientationMode::Y_AXIS;
            break;
        case 2:
            newOrientationMode = OrientationMode::Z_AXIS;
            break;
    }

    // The orientation update setter will change the fragment shader
    m_plane->setOrientationMode(newOrientationMode);

    // Update TF
    this->updateTFWindowing(this->getTransferFunction()->getWindow(), this->getTransferFunction()->getLevel());

    // Update threshold if necessary
    this->updateTFPoints();

    this->updateShaderSliceIndexParameter();
}

//------------------------------------------------------------------------------

void SNegato2D::changeSliceIndex(int _axialIndex, int _frontalIndex, int _sagittalIndex)
{
    ::fwData::Image::sptr image = this->getInOut< ::fwData::Image >(s_IMAGE_INOUT);
    SLM_ASSERT("inout '" + s_IMAGE_INOUT + "' is missing", image);

    m_axialIndex->value()    = _axialIndex;
    m_frontalIndex->value()  = _frontalIndex;
    m_sagittalIndex->value() = _sagittalIndex;

    m_currentSliceIndex = {
        static_cast<float>(_sagittalIndex ) / (static_cast<float>(image->getSize()[0] - 1)),
        static_cast<float>(_frontalIndex  ) / (static_cast<float>(image->getSize()[1] - 1)),
        static_cast<float>(_axialIndex    ) / (static_cast<float>(image->getSize()[2] - 1))
    };

    this->updateShaderSliceIndexParameter();
}

//------------------------------------------------------------------------------

void SNegato2D::updateShaderSliceIndexParameter()
{
    this->getRenderService()->makeCurrent();
    m_plane->changeSlice( m_currentSliceIndex[static_cast<size_t>(m_plane->getOrientationMode())] );

    this->requestRender();
}

//------------------------------------------------------------------------------

void SNegato2D::updateTFPoints()
{
    ::fwData::TransferFunction::sptr tf = this->getTransferFunction();

    m_gpuTF->updateTexture(tf);

    m_plane->switchThresholding(tf->getIsClamped());

    // Sends the TF texture to the negato-related passes
    m_plane->setTFData(*m_gpuTF.get());

    this->requestRender();
}

//-----------------------------------------------------------------------------

void SNegato2D::updateTFWindowing(double /*window*/, double /*leve*/)
{
    ::fwData::TransferFunction::sptr tf = this->getTransferFunction();

    m_gpuTF->updateTexture(tf);

    this->requestRender();
}

//-----------------------------------------------------------------------------

::fwServices::IService::KeyConnectionsMap SNegato2D::getAutoConnections() const
{
    ::fwServices::IService::KeyConnectionsMap connections;
    connections.push( s_IMAGE_INOUT, ::fwData::Image::s_MODIFIED_SIG, s_NEWIMAGE_SLOT );
    connections.push( s_IMAGE_INOUT, ::fwData::Image::s_BUFFER_MODIFIED_SIG, s_NEWIMAGE_SLOT );
    connections.push( s_IMAGE_INOUT, ::fwData::Image::s_SLICE_TYPE_MODIFIED_SIG, s_SLICETYPE_SLOT );
    connections.push( s_IMAGE_INOUT, ::fwData::Image::s_SLICE_INDEX_MODIFIED_SIG, s_SLICEINDEX_SLOT );

    return connections;
}

//------------------------------------------------------------------------------

void SNegato2D::createPlane(const fwData::Image::SpacingType& _spacing)
{
    this->getRenderService()->makeCurrent();
    // Fits the plane to the new texture
    m_plane->setDepthSpacing(_spacing);
    m_plane->initialize2DPlane();
    m_plane->enableAlpha(m_enableAlpha);
}

//------------------------------------------------------------------------------

void SNegato2D::updateCameraWindowBounds()
{

    ::Ogre::Real renderWindowWidth, renderWindowHeight, renderWindowRatio;
    ::Ogre::RenderSystem* renderSystem = getSceneManager()->getDestinationRenderSystem();

    renderWindowWidth  = static_cast< ::Ogre::Real >(renderSystem->_getViewport()->getActualWidth());
    renderWindowHeight = static_cast< ::Ogre::Real >(renderSystem->_getViewport()->getActualHeight());
    renderWindowRatio  = renderWindowWidth / renderWindowHeight;

    ::Ogre::Camera* cam = this->getLayer()->getDefaultCamera();

    if( renderWindowWidth < renderWindowHeight)
    {
        cam->setOrthoWindowWidth(m_plane->getWidth());
    }
    else
    {
        cam->setOrthoWindowHeight(m_plane->getHeight());
    }
    cam->setAspectRatio( renderWindowRatio );

    this->requestRender();
}

//------------------------------------------------------------------------------

} // namespace visuOgreAdaptor
