// Copyright Epic Games, Inc. All Rights Reserved.

import Flutter
import UIKit

public class FlutterTentaclePlugin: NSObject, FlutterPlugin {
  static var tentacleApi: FlutterTentacleApi?
  
  public static func register(with registrar: FlutterPluginRegistrar) {
    tentacleApi = FlutterTentacleApi(binaryMessenger: registrar.messenger())
  }
}
