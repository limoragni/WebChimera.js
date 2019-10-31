const cv = require('opencv4nodejs');
const fileUrl = require('file-url');
const path = require('path');
const webChimera = require('../bin/WebChimera.js.node');

const vlc = webChimera.createPlayer(['--no-audio']);
vlc.pixelFormat = vlc.RV32;
vlc.onFrameReady = onFrameReady
vlc.input.rate = 1.0
vlc.input.rateReverse = 1.0

const videoPath = fileUrl(path.join(__dirname, 'test.mp4'));
const play = false;
const playReverse = false;
const time = 0;
vlc.load(videoPath, play, playReverse, time);

function onFrameReady(videoFrame, frame, time) {
  console.log(`onFrameReady: frame ${frame} - time ${time}ms`);

  const mat = new cv.Mat(videoFrame, videoFrame.height, videoFrame.width, cv.CV_8UC4);
  cv.imshow('frame', mat);
  cv.waitKey(1);
}
