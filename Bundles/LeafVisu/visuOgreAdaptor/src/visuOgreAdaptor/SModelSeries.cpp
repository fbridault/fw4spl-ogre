/* ***** BEGIN LICENSE BLOCK *****
 * FW4SPL - Copyright (C) IRCAD, 2014-2017.
 * Distributed under the terms of the GNU Lesser General Public License (LGPL) as
 * published by the Free Software Foundation.
 * ****** END LICENSE BLOCK ****** */

#include "visuOgreAdaptor/SModelSeries.hpp"

#include "visuOgreAdaptor/SMesh.hpp"
#include "visuOgreAdaptor/SReconstruction.hpp"
#include <visuOgreAdaptor/defines.hpp>

#include <fwCom/Signal.hxx>
#include <fwCom/Slots.hxx>

#include <fwData/Boolean.hpp>
#include <fwData/Material.hpp>
#include <fwData/Mesh.hpp>
#include <fwData/Reconstruction.hpp>
#include <fwData/TransformationMatrix3D.hpp>

#include <fwMedData/ModelSeries.hpp>

#include <fwServices/macros.hpp>
#include <fwServices/op/Add.hpp>

fwServicesRegisterMacro( ::fwRenderOgre::IAdaptor, ::visuOgreAdaptor::SModelSeries, ::fwMedData::ModelSeries);

namespace visuOgreAdaptor
{
//-----------------------------------------------------------------------------

static const ::fwCom::Slots::SlotKeyType s_ADD_RECONSTRUCTION_SLOT    = "addReconstruction";
static const ::fwCom::Slots::SlotKeyType s_REMOVE_RECONSTRUCTION_SLOT = "removeReconstruction";

static const std::string s_MODEL_INPUT = "model";

//------------------------------------------------------------------------------

SModelSeries::SModelSeries() noexcept :
    m_autoResetCamera(true),
    m_materialTemplateName(SMaterial::DEFAULT_MATERIAL_TEMPLATE_NAME),
    m_isDynamic(false),
    m_isDynamicVertices(false)
{
    newSlot(s_ADD_RECONSTRUCTION_SLOT, &SModelSeries::addReconstruction, this);
    newSlot(s_REMOVE_RECONSTRUCTION_SLOT, &SModelSeries::removeReconstruction, this);
}

//------------------------------------------------------------------------------

SModelSeries::~SModelSeries() noexcept
{
}

//------------------------------------------------------------------------------

void SModelSeries::configuring()
{
    this->configureParams();

    const ConfigType config = this->getConfigTree().get_child("service.config.<xmlattr>");

    if (config.count("transform"))
    {
        this->setTransformId(config.get<std::string>("transform"));
    }

    if (config.count("autoresetcamera"))
    {
        m_autoResetCamera = config.get<std::string>("autoresetcamera") == "yes";
    }

    if (config.count("material"))
    {
        m_materialTemplateName = config.get<std::string>("material");
    }

    if(config.count("dynamic"))
    {
        m_isDynamic = config.get<bool>("dynamic");
    }

    if(config.count("dynamicVertices"))
    {
        m_isDynamicVertices = config.get<bool>("dynamicVertices");
    }
}

//------------------------------------------------------------------------------

void SModelSeries::starting()
{
    this->initialize();

    this->updating();
}

//------------------------------------------------------------------------------

void SModelSeries::updating()
{
    // Retrieves the associated f4s ModelSeries object
    const auto modelSeries = this->getInput< ::fwMedData::ModelSeries >(s_MODEL_INPUT);

    this->stopping();

    // showRec indicates if we have to show the associated reconstructions or not
    const bool showRec = modelSeries->getField("ShowReconstructions", ::fwData::Boolean::New(true))->value();

    for(auto reconstruction : modelSeries->getReconstructionDB())
    {
        auto adaptor = this->registerService< ::visuOgreAdaptor::SReconstruction>("::visuOgreAdaptor::SReconstruction");
        adaptor->registerInput(reconstruction, "reconstruction", true);

        // We use the default service ID to get a unique number because a ModelSeries contains several Reconstructions
        adaptor->setID(this->getID() + "_" + adaptor->getID());

        adaptor->setRenderService(this->getRenderService());
        adaptor->setLayerID(m_layerID);
        adaptor->setTransformId(adaptor->getID() + "_TF");
        adaptor->setMaterialTemplateName(m_materialTemplateName);
        adaptor->setParentTransformId(this->getTransformId());
        adaptor->setAutoResetCamera(m_autoResetCamera);

        adaptor->start();
        adaptor->setForceHide(!showRec);

        ::visuOgreAdaptor::SMesh::sptr meshAdaptor = adaptor->getMeshAdaptor();
        meshAdaptor->setDynamic(m_isDynamic);
        meshAdaptor->setDynamicVertices(m_isDynamicVertices);
    }
}

//------------------------------------------------------------------------------

void SModelSeries::stopping()
{
    this->unregisterServices();
}

//------------------------------------------------------------------------------

void SModelSeries::addReconstruction()
{
    this->updating();
}

//------------------------------------------------------------------------------

void SModelSeries::removeReconstruction()
{
    this->stopping();
}

//-----------------------------------------------------------------------------

::fwServices::IService::KeyConnectionsMap SModelSeries::getAutoConnections() const
{
    ::fwServices::IService::KeyConnectionsMap connections;
    connections.push( s_MODEL_INPUT, ::fwMedData::ModelSeries::s_MODIFIED_SIG, s_UPDATE_SLOT );
    connections.push( s_MODEL_INPUT, ::fwMedData::ModelSeries::s_RECONSTRUCTIONS_ADDED_SIG, s_ADD_RECONSTRUCTION_SLOT);
    connections.push( s_MODEL_INPUT, ::fwMedData::ModelSeries::s_RECONSTRUCTIONS_REMOVED_SIG,
                      s_REMOVE_RECONSTRUCTION_SLOT );
    return connections;
}

//------------------------------------------------------------------------------

} // namespace visuOgreAdaptor
