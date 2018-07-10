/* ***** BEGIN LICENSE BLOCK *****
 * FW4SPL - Copyright (C) IRCAD, 2018.
 * Distributed under the terms of the GNU Lesser General Public License (LGPL) as
 * published by the Free Software Foundation.
 * ****** END LICENSE BLOCK ****** */

#include "TextTest.hpp"

#include <fwRenderOgre/helper/Font.hpp>
#include <fwRenderOgre/Text.hpp>
#include <fwRenderOgre/Utils.hpp>

#include <OGRE/OgreLogManager.h>
#include <OGRE/OgreMaterialManager.h>
#include <OGRE/OgreRenderWindow.h>
#include <OGRE/OgreSceneNode.h>
#include <OGRE/OgreTextureManager.h>
#include <OGRE/Overlay/OgreOverlayManager.h>

CPPUNIT_TEST_SUITE_REGISTRATION( ::fwRenderOgre::ut::TextTest );

namespace fwRenderOgre
{

namespace ut
{

//------------------------------------------------------------------------------

TextTest::TextTest()
{

}

//------------------------------------------------------------------------------

TextTest::~TextTest()
{

}

//------------------------------------------------------------------------------

void TextTest::setUp()
{
    m_ogreRoot = Utils::getOgreRoot();

    // Don't output the log to the terminal and delete the file when the test is done.
    ::Ogre::LogManager* logMgr = ::Ogre::LogManager::getSingletonPtr();
    logMgr->createLog("OgreTest.log", true, false, true);
}

//------------------------------------------------------------------------------

void TextTest::tearDown()
{
    m_ogreRoot = nullptr;
    Utils::destroyOgreRoot();
}

//------------------------------------------------------------------------------

void TextTest::factoryTest()
{
    auto ogreRenderWindow = m_ogreRoot->createRenderWindow("Dummy-RenderWindow",
                                                           static_cast<unsigned int>(1),
                                                           static_cast<unsigned int>(1),
                                                           false,
                                                           nullptr);
    ogreRenderWindow->setVisible(false);
    ogreRenderWindow->setAutoUpdated(false);
    ::Ogre::ResourceGroupManager::getSingleton().initialiseAllResourceGroups();

    // Load the material manually because the Font will need it
    ::Ogre::MaterialManager::getSingleton().load("Text", ::Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);

    ::Ogre::SceneManager* sceneManager = m_ogreRoot->createSceneManager("DefaultSceneManager", "test");
    const auto& factoryName = ::fwRenderOgre::factory::Text::FACTORY_TYPE_NAME;
    const auto& textName1   = "COUCOU";

    ::Ogre::SceneNode* rootNode = sceneManager->getRootSceneNode();

    auto& overlayManager = ::Ogre::OverlayManager::getSingleton();

    auto overlayTextPanel =
        static_cast< ::Ogre::OverlayContainer* >(overlayManager.createOverlayElement("Panel", "_GUI"));

    ::Ogre::FontPtr courrierFont = ::fwRenderOgre::helper::Font::getFont("Courier.ttf", 32);

    ::fwRenderOgre::Text* textObj1 = ::fwRenderOgre::Text::New("testTest", sceneManager, overlayTextPanel,
                                                               courrierFont);
    CPPUNIT_ASSERT(textObj1 != nullptr); // See if it has the right type.

    ::Ogre::MovableObject* movableText1 = textObj1;
    CPPUNIT_ASSERT(movableText1 != nullptr); // Check if the object was created.

    rootNode->attachObject(textObj1);
    CPPUNIT_ASSERT_EQUAL(size_t(1), rootNode->getAttachedObjects().size());
    CPPUNIT_ASSERT_EQUAL(movableText1, rootNode->getAttachedObject(0));

    rootNode->detachObject(movableText1);
    CPPUNIT_ASSERT_EQUAL(size_t(0), rootNode->getAttachedObjects().size());

    sceneManager->destroyMovableObject(textObj1);
    CPPUNIT_ASSERT_EQUAL(false, sceneManager->hasMovableObject(textName1, factoryName));

    auto movableObjIterator = sceneManager->getMovableObjectIterator(factoryName);
    CPPUNIT_ASSERT(movableObjIterator.end() == movableObjIterator.current());

    m_ogreRoot->destroySceneManager(sceneManager);

    ogreRenderWindow->destroy();
}

//------------------------------------------------------------------------------

} //namespace ut
} //namespace fwRenderOgre
