// Copyright Epic Games, Inc. All Rights Reserved.

import UIKit
import Flutter
import WebRTC
import SpriteKit

@main
@objc class AppDelegate: FlutterAppDelegate {
  var peerConnectionApi: FlutterRtcPeerConnectionApi?
  var dataChannelApi: FlutterRtcDataChannelApi?
  var videoViewControllerApi: FlutterRtcVideoViewControllerApi?
  var arSessionApi: FlutterArSessionApi?
  var gamepadApi: FlutterGamepadApi?
  
  override func application(
    _ application: UIApplication,
    didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?
  ) -> Bool {
    RTCInitializeSSL()
    
    GeneratedPluginRegistrant.register(with: self)
    
    // Register Pigeon APIs
    let flutterViewController = (window?.rootViewController as! FlutterViewController)
    let binaryMessenger = flutterViewController.binaryMessenger
    
    peerConnectionApi = FlutterRtcPeerConnectionApi(binaryMessenger: binaryMessenger)
    dataChannelApi = FlutterRtcDataChannelApi(binaryMessenger: binaryMessenger)
    videoViewControllerApi = FlutterRtcVideoViewControllerApi(binaryMessenger: binaryMessenger, textureRegistry: flutterViewController)
    arSessionApi = FlutterArSessionApi(binaryMessenger: binaryMessenger)
    gamepadApi = FlutterGamepadApi(binaryMessenger: binaryMessenger)
  
    return super.application(application, didFinishLaunchingWithOptions: launchOptions)
  }
  
  override func applicationWillTerminate(_ application: UIApplication) {
    peerConnectionApi?.disposeAll()
  }
}
