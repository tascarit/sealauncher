import QtQuick 2.15
import QtQuick.Window 2.15
import QtMultimedia
import QtQuick.Controls 2.15

Item {
    width: parent
    height: parent
    clip: true

    Video {
        id: backgroundVideo
        source: "qrc:/resources/background.webm"
        loops: MediaPlayer.Infinite
        anchors.fill: parent
        clip: true
        muted: true
        VideoOutput: videoOutput

        Component.onCompleted: {
            backgroundVideo.play()
        }
    }

    VideoOutput {
        id: videoOutput
        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectCrop

    }
}
