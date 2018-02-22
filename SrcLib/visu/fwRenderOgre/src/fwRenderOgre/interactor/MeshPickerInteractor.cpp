/* ***** BEGIN LICENSE BLOCK *****
 * FW4SPL - Copyright (C) IRCAD, 2014-2018.
 * Distributed under the terms of the GNU Lesser General Public License (LGPL) as
 * published by the Free Software Foundation.
 * ****** END LICENSE BLOCK ****** */

#include "fwRenderOgre/interactor/MeshPickerInteractor.hpp"

#include <fwRenderOgre/interactor/IInteractor.hpp>
#include <fwRenderOgre/registry/macros.hpp>

#include <fwCom/Signal.hxx>

fwRenderOgreRegisterInteractorMacro( ::fwRenderOgre::interactor::MeshPickerInteractor );

namespace fwRenderOgre
{

namespace interactor
{

MeshPickerInteractor::MeshPickerInteractor() noexcept
{
}

//------------------------------------------------------------------------------

MeshPickerInteractor::~MeshPickerInteractor() noexcept
{
}

//------------------------------------------------------------------------------

bool MeshPickerInteractor::mouseClickEvent(int x, int y, int width, int height)
{
    if(m_picker->hasSceneManager())
    {
        if(m_picker->executeRaySceneQuery(x, y, width, height, m_queryFlags))
        {
            ::Ogre::Vector3 click = m_picker->getIntersectionInWorldSpace();

            ::fwData::Point::sptr point                = fwData::Point::New();
            ::fwData::Point::PointCoordArrayType cords =
            {{static_cast<double>(click.x), static_cast<double>(click.y), static_cast<double>(click.z)}};
            point->setCoord(cords);

            m_sigPointClicked->asyncEmit(::fwData::Object::dynamicCast(point));
            return true;
        }
    }
    else
    {
        SLM_WARN("The picker scene hasn't been initialized, you are not using this interactor correctly");
    }
    return false;
}

//------------------------------------------------------------------------------

} //namespace interactor
} //namespace fwRenderOgre
