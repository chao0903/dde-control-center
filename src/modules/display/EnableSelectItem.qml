/****************************************************************************
**
**  Copyright (C) 2011~2014 Deepin, Inc.
**                2011~2014 Kaisheng Ye
**
**  Author:     Kaisheng Ye <kaisheng.ye@gmail.com>
**  Maintainer: Kaisheng Ye <kaisheng.ye@gmail.com>
**
**  This program is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  any later version.
**
**  This program is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with this program.  If not, see <http://www.gnu.org/licenses/>.
**
****************************************************************************/

import QtQuick 2.0
import Deepin.Widgets 1.0

Item {
    id: selectItem
    width: parent.width
    height: 28
    property int labelLeftMargin: 40
    property bool selected: typeof(pOutputObj) == "undefined" ? false : pOutputObj.opened
    property bool hovered: false

    property var pOutputObj
    property string pOutputObjName: ""

    signal selectAction(bool flag)

//    Rectangle {
//        visible: selected
//        anchors.left: parent.left
//        anchors.right: parent.right
//        anchors.leftMargin: 10
//        anchors.rightMargin: 10
//        height: 28
//        color: "#0D0D0D"
//        radius: 4
//    }

    Image {
        id: nameImage
        anchors.left: parent.left
        anchors.leftMargin: 18
        visible: selected
        width: 16
        fillMode: Image.PreserveAspectFit
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: nameText.left
        source: "images/select.png"
    }

    DssH3 {
        id: nameText
        width: parent.width - nameImage.width
        anchors.left: parent.left
        anchors.leftMargin: labelLeftMargin
        anchors.verticalCenter: parent.verticalCenter
        text: pOutputObjName
        //elide: Text.ElideRight

        property bool isElide: false

        color: {
            if(selected){
                return DConstants.activeColor
            }else if(hovered){
                return DConstants.hoverColor
            }
            else{
                return DConstants.fgColor
            }
        }
        font.pixelSize: 12

        Component.onCompleted: {
            if(width < contentWidth){
                elide = Text.ElideRight
                isElide = true
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true

        onEntered: {
            hovered = true
        }

        onExited: {
            hovered = false
        }

        onClicked: {
            selectAction(!selected)
        }
    }

    ListView.onAdd: SequentialAnimation {
        PropertyAction { target: selectItem; property: "height"; value: 0 }
        NumberAnimation { target: selectItem; property: "height"; to: 28; duration: 250; easing.type: Easing.InOutQuad }
    }

    ListView.onRemove: SequentialAnimation {
        PropertyAction { target: selectItem; property: "ListView.delayRemove"; value: true }
        NumberAnimation { target: selectItem; property: "height"; to: 0; duration: 250; easing.type: Easing.InOutQuad }

        // Make sure delayRemove is set back to false so that the item can be destroyed
        PropertyAction { target: selectItem; property: "ListView.delayRemove"; value: false }
    }

}
