
set( NAME ExSimpleARCVOgre )
set( VERSION 0.1 )
set( TYPE APP )
set( DEPENDENCIES  )
set( REQUIREMENTS

    appXml
    arDataReg
    arMedia
    ctrlCamp
    dataReg
    fwlauncher
    gui
    guiQt
    ioCalibration
    uiPreferences
    uiTools
    maths
    preferences
    servicesReg
    trackerAruco
    registrationCV
    videoQt
    videoTools
    videoCalibration
    visuOgre
    visuOgreAdaptor
    visuOgreQt
    ioVTK
    )

bundleParam(appXml PARAM_LIST config PARAM_VALUES ExSimpleARCVOgreConfig)