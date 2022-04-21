# -*- coding: utf-8 -*-
"""QGIS Unit tests for QgsVectorLayerElevationProperties

.. note:: This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
"""
__author__ = 'Nyall Dawson'
__date__ = '09/11/2020'
__copyright__ = 'Copyright 2020, The QGIS Project'

import qgis  # NOQA

from qgis.core import (
    Qgis,
    QgsVectorLayerElevationProperties,
    QgsReadWriteContext,
    QgsLineSymbol,
    QgsMarkerSymbol,
    QgsFillSymbol,
    QgsProperty,
    QgsMapLayerElevationProperties,
    QgsPropertyCollection
)

from qgis.PyQt.QtXml import QDomDocument

from qgis.testing import start_app, unittest

start_app()


class TestQgsVectorLayerElevationProperties(unittest.TestCase):

    def testBasic(self):
        props = QgsVectorLayerElevationProperties(None)
        self.assertEqual(props.zScale(), 1)
        self.assertEqual(props.zOffset(), 0)
        self.assertFalse(props.extrusionEnabled())
        self.assertEqual(props.extrusionHeight(), 0)
        self.assertFalse(props.hasElevation())
        self.assertEqual(props.clamping(), Qgis.AltitudeClamping.Terrain)
        self.assertEqual(props.binding(), Qgis.AltitudeBinding.Centroid)
        self.assertTrue(props.respectLayerSymbology())

        props.setZOffset(0.5)
        props.setZScale(2)
        props.setClamping(Qgis.AltitudeClamping.Relative)
        props.setBinding(Qgis.AltitudeBinding.Vertex)
        props.setExtrusionHeight(10)
        props.setExtrusionEnabled(True)
        props.setRespectLayerSymbology(False)
        self.assertEqual(props.zScale(), 2)
        self.assertEqual(props.zOffset(), 0.5)
        self.assertEqual(props.extrusionHeight(), 10)
        self.assertTrue(props.hasElevation())
        self.assertTrue(props.extrusionEnabled())
        self.assertEqual(props.clamping(), Qgis.AltitudeClamping.Relative)
        self.assertEqual(props.binding(), Qgis.AltitudeBinding.Vertex)
        self.assertFalse(props.respectLayerSymbology())

        props.dataDefinedProperties().setProperty(QgsMapLayerElevationProperties.ExtrusionHeight, QgsProperty.fromExpression('1*5'))
        self.assertEqual(props.dataDefinedProperties().property(QgsMapLayerElevationProperties.ExtrusionHeight).asExpression(), '1*5')
        properties = QgsPropertyCollection()
        properties.setProperty(QgsMapLayerElevationProperties.ZOffset, QgsProperty.fromExpression('9'))
        props.setDataDefinedProperties(properties)
        self.assertFalse(
            props.dataDefinedProperties().isActive(QgsMapLayerElevationProperties.ExtrusionHeight))
        self.assertEqual(
            props.dataDefinedProperties().property(QgsMapLayerElevationProperties.ZOffset).asExpression(),
            '9')

        sym = QgsLineSymbol.createSimple({'outline_color': '#ff4433', 'outline_width': 0.5})
        props.setProfileLineSymbol(sym)
        self.assertEqual(props.profileLineSymbol().color().name(), '#ff4433')

        sym = QgsFillSymbol.createSimple({'color': '#ff4455', 'outline_width': 0.5})
        props.setProfileFillSymbol(sym)
        self.assertEqual(props.profileFillSymbol().color().name(), '#ff4455')

        sym = QgsMarkerSymbol.createSimple({'color': '#ff1122', 'outline_width': 0.5})
        props.setProfileMarkerSymbol(sym)
        self.assertEqual(props.profileMarkerSymbol().color().name(), '#ff1122')

        doc = QDomDocument("testdoc")
        elem = doc.createElement('test')
        props.writeXml(elem, doc, QgsReadWriteContext())

        props2 = QgsVectorLayerElevationProperties(None)
        props2.readXml(elem, QgsReadWriteContext())
        self.assertEqual(props2.zScale(), 2)
        self.assertEqual(props2.zOffset(), 0.5)
        self.assertEqual(props2.clamping(), Qgis.AltitudeClamping.Relative)
        self.assertEqual(props2.binding(), Qgis.AltitudeBinding.Vertex)
        self.assertEqual(props2.extrusionHeight(), 10)
        self.assertTrue(props2.extrusionEnabled())
        self.assertFalse(props2.respectLayerSymbology())

        self.assertEqual(props2.profileLineSymbol().color().name(), '#ff4433')
        self.assertEqual(props2.profileFillSymbol().color().name(), '#ff4455')
        self.assertEqual(props2.profileMarkerSymbol().color().name(), '#ff1122')

        self.assertEqual(
            props2.dataDefinedProperties().property(QgsMapLayerElevationProperties.ZOffset).asExpression(),
            '9')

        props_clone = props.clone()
        self.assertEqual(props_clone.zScale(), 2)
        self.assertEqual(props_clone.zOffset(), 0.5)
        self.assertEqual(props_clone.clamping(), Qgis.AltitudeClamping.Relative)
        self.assertEqual(props_clone.binding(), Qgis.AltitudeBinding.Vertex)
        self.assertEqual(props_clone.extrusionHeight(), 10)
        self.assertTrue(props_clone.extrusionEnabled())
        self.assertFalse(props_clone.respectLayerSymbology())

        self.assertEqual(props_clone.profileLineSymbol().color().name(), '#ff4433')
        self.assertEqual(props_clone.profileFillSymbol().color().name(), '#ff4455')
        self.assertEqual(props_clone.profileMarkerSymbol().color().name(), '#ff1122')

        self.assertEqual(
            props_clone.dataDefinedProperties().property(QgsMapLayerElevationProperties.ZOffset).asExpression(),
            '9')


if __name__ == '__main__':
    unittest.main()
