// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:ui';
import 'dart:async';

import 'package:fidl/fidl.dart' as fidl;
import 'package:fidl_fuchsia_ui_app/fidl_async.dart';
import 'package:fidl_fuchsia_ui_test_input/fidl_async.dart' as test_touch;
import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:flutter/material.dart';
import 'package:fuchsia_scenic_flutter/fuchsia_view.dart';
import 'package:fuchsia_services/services.dart';
import 'package:zircon/zircon.dart';

Future<void> main(List<String> args) async {
  WidgetsFlutterBinding.ensureInitialized();

  runApp(MaterialApp(debugShowCheckedModeBanner: false, home: TestApp()));
}

// Creates an app that changes color and calls test.touch.ResponseListenter.Respond when tapped.
// Launches a child app that takes up the left half of the display on startup.
class TestApp extends StatefulWidget {
  @override
  _TestAppState createState() => _TestAppState();
}

class _TestAppState extends State<TestApp> {
  static const _red = Color.fromARGB(255, 255, 0, 0);
  static const _blue = Color.fromARGB(255, 0, 0, 255);

  final _context = ComponentContext.create();

  final _connection = ValueNotifier<FuchsiaViewConnection>(null);
  final _responseListener = test_touch.TouchInputListenerProxy();
  final _backgroundColor = ValueNotifier(_blue);

  _TestAppState() {
    Incoming.fromSvcPath()
      ..connectToService(_responseListener)
      ..close();

    _connection.value = FuchsiaViewConnection(_launchApp('child-view'),
        onViewStateChanged: (_, state) {
      if (state) {
        print('Child view ready');
      }
    });

    // We inspect the lower-level data packets, instead of using the higher-level gesture library.
    WidgetsBinding.instance.window.onPointerDataPacket =
        (PointerDataPacket packet) {
      // Record the time when the pointer event was received.
      int nowNanos = System.clockGetMonotonic();

      for (PointerData data in packet.data) {
        print('Flutter received a pointer: ${data.toStringFull()}');
        if (data.change == PointerChange.down) {
          if (_backgroundColor.value == _blue) {
            _backgroundColor.value = _red;
          } else {
            _backgroundColor.value = _blue;
          }

          _responseListener.reportTouchInput(
              test_touch.TouchInputListenerReportTouchInputRequest(
                  // Notify test that input was seen.
                  localX: data.physicalX,
                  localY: data.physicalY,
                  timeReceived: nowNanos,
                  componentName: 'embedding-flutter'));
        }
      }
    };
  }

  @override
  Widget build(BuildContext context) {
    return ValueListenableBuilder(
        valueListenable: _backgroundColor,
        builder: (context, _, __) {
          return ValueListenableBuilder(
              valueListenable: _connection,
              builder: (context, ___, _____) {
                return Container(
                  color: _backgroundColor.value,
                  child: Stack(
                    alignment: Alignment.centerLeft,
                    children: [
                      if (_connection.value != null)
                        FractionallySizedBox(
                          widthFactor: 0.5,
                          heightFactor: 1,
                          child: FuchsiaView(
                            controller: _connection.value,
                            hitTestable: true,
                            focusable: true,
                          ),
                        ),
                    ],
                  ),
                );
              });
        });
  }
}

ViewHolderToken _launchApp(String debugName) {
  print('Launching child : $debugName');

  // Call fuchsia.ui.app.ViewProvider::createView to launch the embedded app.
  ViewProviderProxy viewProvider = ViewProviderProxy();
  Incoming.fromSvcPath()
    ..connectToService(viewProvider)
    ..close();

  EventPairPair viewTokens = EventPairPair();
  assert(viewTokens.status == ZX.OK);
  ViewHolderToken viewHolderToken = ViewHolderToken(value: viewTokens.first);
  ViewToken viewToken = ViewToken(value: viewTokens.second);

  viewProvider.createView(viewToken.value, null, null);
  viewProvider.ctrl.close();

  return viewHolderToken;
}
