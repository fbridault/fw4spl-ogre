/* ***** BEGIN LICENSE BLOCK *****
 * FW4SPL - Copyright (C) IRCAD, 2014-2017.
 * Distributed under the terms of the GNU Lesser General Public License (LGPL) as
 * published by the Free Software Foundation.
 * ****** END LICENSE BLOCK ****** */

#include "fwRenderOgre/ILight.hpp"

#include <fwServices/registry/ObjectService.hpp>

#include <string>

namespace fwRenderOgre
{

//-----------------------------------------------------------------------------

const ::fwRenderOgre::ILight::FactoryRegistryKeyType ILight::REGISTRY_KEY = "::fwRenderOgre::ILight::REGISTRY_KEY";

const std::string ILight::s_POINT_LIGHT       = "Point";
const std::string ILight::s_DIRECTIONAL_LIGHT = "Directional";
const std::string ILight::s_SPOT_LIGHT        = "Spot";

const int ::fwRenderOgre::ILight::s_OFFSET_RANGE = 180;

//-----------------------------------------------------------------------------

::fwRenderOgre::ILight::sptr ILight::createLightAdaptor(::fwData::TransformationMatrix3D::sptr _transform,
                                                        ::fwData::Color::sptr _diffuse,
                                                        ::fwData::Color::sptr _specular)
{
    ::fwRenderOgre::ILight::sptr light = ::fwRenderOgre::lightFactory::New(::fwRenderOgre::ILight::REGISTRY_KEY );
    SLM_ASSERT("The factory process to create an ILight failed.", light);
    SLM_ASSERT("The light adaptor must be registered with existing data objects.", _transform && _diffuse && _specular);

    light->registerInOut(_transform, "transform", true);
    light->registerInOut(_diffuse, "diffuseColor", true);
    light->registerInOut(_specular, "specularColor", true);

    return light;
}

//-----------------------------------------------------------------------------

void ILight::destroyLightAdaptor(ILight::sptr _lightAdaptor)
{
    if(_lightAdaptor)
    {
        _lightAdaptor->stop();
        ::fwServices::OSR::unregisterService(_lightAdaptor);
    }
}

//-----------------------------------------------------------------------------

::fwRenderOgre::ILight::ILight()
{
}

//-----------------------------------------------------------------------------

::fwRenderOgre::ILight::~ILight()
{
}

//-----------------------------------------------------------------------------

} // namespace fwRenderOgre

