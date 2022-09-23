// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:convert';
import 'dart:io';

import 'package:meta/meta.dart';
import 'package:sl4f/sl4f.dart';
import 'package:sl4f/trace_processing.dart' as trace;
import 'package:test/test.dart';

import 'helpers.dart';

enum Protocol { udp, tcp }

enum Direction { deviceToHost, hostToDevice, loopback }

// This test adds e2e performance benchmarks using iperf3 for TCP and UDP
// traffic. This runs iperf3 in 2 ways:
// - client and server both running in Fuchsia over loopback.
// - server running in Fuchsia and client on the host.
//
// Some of the benchmarks are:
// throughput - send/recv rate of transfer in bits/second
// loss       - number of UDP packets that were sent but failed to reach the
//              receiving application.
// jitter     - mean deviation of latency experienced by receiver in msec for UDP.
// CPU usage  - Avg CPU usage during the iperf3 sessions.
void main(List<String> args) {
  enableLoggingOutput();
  // TCP/UDP port number that the Fuchsia side will listen on.
  const port = 9001;
  // systemMetricsStarted indicates if we have waited for system_metrics
  // daemon to be launched.
  bool systemMetricsStarted = false;

  // Process the json output from iperf3 and convert that to Catapult format.
  // 'send' indicates if the iperf output has device transmit stats.
  // 'recv' indicates if the iperf output has device receive stats.
  // 'deviceLocal' if the iperf sessions were local to the device over loopback.
  Future<void> processIperfResults(
      {@required Direction direction,
      @required PerfTestHelper helper,
      @required String resultsFile,
      @required List<double> cpuPercentages,
      @required String expectedMetricNamesFile}) async {
    File localResultsFile;
    if (direction == Direction.loopback) {
      // Make a local copy of the json file generated by iperf3.
      localResultsFile =
          await helper.storage.dumpFile(resultsFile, 'iperf', 'json');
    } else {
      localResultsFile = File(resultsFile);
    }

    final List<Map<String, dynamic>> results = [];
    var iperfResults = jsonDecode(await localResultsFile.readAsString());
    // Retrieve records from iperf json.
    var setup = iperfResults['start']['test_start'];
    var protocol = setup['protocol'];
    var msgSize = setup['blksize'];
    var end = iperfResults['end'];
    var testSuite = 'fuchsia.netstack.iperf_benchmarks';

    Map<String, dynamic> sender;
    Map<String, dynamic> receiver;

    if (protocol == 'TCP') {
      sender = end['sum_sent'];
      receiver = end['sum_received'];
    } else {
      sender = end['sum'];
      // For UDP, there is no sum_received record, but we gather the
      // receiver information from server-output.
      var serverOutput = iperfResults['server_output_json'];
      receiver = serverOutput['end']['sum'];
      // server output summary contains only the sent-bytes, but
      // the interval data has the bytes transferred which we can
      // use to compute the received bit rate.
      if (!receiver['sender']) {
        var intervals = serverOutput['intervals'];
        int bytesTransferred = 0;
        for (var i in intervals) {
          bytesTransferred += i['sum']['bytes'];
        }
        receiver['bits_per_second'] =
            (bytesTransferred * 8) / receiver['seconds'];
      }
    }

    String unit(String key) {
      switch (key) {
        case 'bits_per_second':
          return 'bits/second';
        case 'lost_packets':
          return 'count';
        case 'jitter_ms':
          return 'milliseconds';
        default:
          return 'unknown';
      }
    }

    String directionLabel;
    switch (direction) {
      case Direction.deviceToHost:
        directionLabel = 'send';
        break;
      case Direction.hostToDevice:
        directionLabel = 'recv';
        break;
      case Direction.loopback:
        directionLabel = 'loopback';
        break;
    }

    // Translate iperf json to fuchsiaperf.json.
    if (direction == Direction.deviceToHost) {
      var key = 'bits_per_second';
      results.add({
        'label': '$protocol/$directionLabel/${msgSize}bytes/$key',
        'test_suite': testSuite,
        'unit': unit(key),
        'values': [sender[key]]
      });
    } else {
      for (var key in receiver.keys) {
        if (key == 'bits_per_second' ||
            key == 'lost_packets' ||
            key == 'jitter_ms') {
          results.add({
            'label': '$protocol/$directionLabel/${msgSize}bytes/$key',
            'test_suite': testSuite,
            'unit': unit(key),
            'values': [receiver[key]]
          });
        }
      }
    }

    results.add({
      'label': '$protocol/$directionLabel/${msgSize}bytes/CPU',
      'test_suite': testSuite,
      'unit': 'percent',
      'values': cpuPercentages
    });

    var fuchsiaperfFile = await Dump().writeAsString(
        'netstack_iperf_results', 'fuchsiaperf.json', jsonEncode(results));
    // Translate fuchsiaperf.json to Catapult format.
    await helper.performance.convertResults('runtime_deps/catapult_converter',
        fuchsiaperfFile, Platform.environment,
        expectedMetricNamesFile: expectedMetricNamesFile);
  }

  Future<void> startIperfServer(PerfTestHelper helper) async {
    // Start iperf server on the target device.
    await helper.sl4fDriver.ssh.start('iperf3 --server --port $port --json',
        mode: ProcessStartMode.detached);

    // Poll for the server to have started listening for client
    // connection on the expected port.
    for (var i = 0; i < 15; i++) {
      await Future.delayed(Duration(seconds: 1));
      final inspect = Inspect(helper.sl4fDriver);
      final results = await inspect
          .snapshot(['core/network/netstack:Socket\\ Info/*:LocalAddress']);
      if (results != null && results.isNotEmpty) {
        for (var j = 0; j < results.length; j++) {
          if (results[j]['payload'] != null &&
              results[j]['payload']['Socket Info'] != null) {
            for (final entry in results[j]['payload']['Socket Info'].entries) {
              if (entry.value['LocalAddress'] != null &&
                  entry.value['LocalAddress'].endsWith(':$port')) {
                return;
              }
            }
          }
        }
      }
    }
    throw Sl4fException('Failed to start Iperf server.');
  }

  Future<Iterable> getCpuUsages(Performance performance, File traceFile) async {
    const String _trace2jsonPath = 'runtime_deps/trace2json';
    final traceResults =
        await performance.convertTraceFileToJson(_trace2jsonPath, traceFile);

    final model = await trace.createModelFromFile(traceResults);
    return trace.getArgValuesFromEvents<double>(
        trace.filterEventsTyped<trace.CounterEvent>(trace.getAllEvents(model),
            category: 'system_metrics', name: 'cpu_usage'),
        'average_cpu_percentage');
  }

  // Wait for system_metrics daemon to start logging CPU usage metrics.
  // At the time of writing this test, on NUC7, this sometimes can take
  // ~30sec for us to start getting CPU usage metrics.
  Future<void> waitSystemMetricsDaemonStart(PerfTestHelper helper) async {
    if (systemMetricsStarted) {
      return;
    }

    for (var i = 0; i < 10; i++) {
      final performance = Performance(helper.sl4fDriver, Dump());
      final traceSession = await performance.initializeTracing(categories: [
        'system_metrics',
      ]);
      await traceSession.start();
      // Do nothing for sometime to let system_metrics to be logged.
      await Future.delayed(Duration(seconds: 10));
      await traceSession.stop();
      final cpuPercentages = await getCpuUsages(
          performance, await traceSession.terminateAndDownload('iperf'));
      if (cpuPercentages.isNotEmpty) {
        systemMetricsStarted = true;
        return;
      }
    }
    throw Sl4fException(
        'Failed to retrieve CPU stats from system_metrics daemon.');
  }

  Future<void> runIperfClientTests(PerfTestHelper helper, Protocol proto,
      String label, Direction direction) async {
    String protocolOption = '';
    String bwValue = '';
    String dirOption = '';
    String serverIp;
    if (proto == Protocol.udp) {
      protocolOption = '--udp';
    }

    if (direction == Direction.loopback) {
      // For localhost test, use maximum possible bandwidth.
      bwValue = '0';
    } else {
      // TODO(fxbug.dev/47782): Until we define separate link for ssh and data,
      // enforce a < 1Gbps rate on NUC7. After the bug is resolved, this can
      // be changed to '0' which means as much as the system and link can
      // transmit.
      bwValue = '100M';
    }

    if (direction == Direction.deviceToHost) {
      // This reverses the default direction of traffic flow such that
      // the target device sends traffic.
      dirOption = '--reverse';
    }

    if (direction == Direction.loopback) {
      serverIp = '127.0.0.1';
    } else {
      // TODO(fxbug.dev/47782): Currently, we are using the link used for ssh to also
      // inject data traffic. This is prone to interference to ssh and to the tests.
      // On NUC7, we can use a separate usb-ethernet interface for the test traffic.
      serverIp = helper.sl4fDriver.ssh.target;
    }

    var msgSizes = {64, 1024, 1400};
    for (var size in msgSizes) {
      try {
        var cmdArgs = [
          '--client',
          serverIp,
          '--port',
          '$port',
          '--length',
          '$size',
          '--json',
          protocolOption,
          '--bitrate',
          '$bwValue',
          dirOption,
          '--get-server-output'
        ];
        await startIperfServer(helper);
        // Start tracing.
        final performance = Performance(helper.sl4fDriver, Dump());
        final traceSession = await performance.initializeTracing(categories: [
          'system_metrics',
        ]);
        await traceSession.start();

        String resultsFile;
        if (direction == Direction.loopback) {
          resultsFile = '/tmp/iperf_results.json';
          var args = cmdArgs.join(' ');
          final result =
              await helper.sl4fDriver.ssh.run('iperf3 $args > $resultsFile');
          expect(result.exitCode, equals(0));
        } else {
          // Run iperf3 client from the host-tools.
          final hostPath =
              Platform.script.resolve('runtime_deps/iperf3').toFilePath();
          final result = await Process.run(hostPath, cmdArgs, runInShell: true);
          var iperfFile =
              await Dump().writeAsString('iperf', 'json', result.stdout);
          resultsFile = iperfFile.path;
        }

        // Wait for trace record to complete.
        await traceSession.stop();
        final cpuPercentages = await getCpuUsages(
            performance, await traceSession.terminateAndDownload('iperf'));

        await processIperfResults(
            helper: helper,
            resultsFile: resultsFile,
            cpuPercentages: cpuPercentages.toList(),
            direction: direction,
            expectedMetricNamesFile:
                'fuchsia.netstack.iperf_benchmarks.${label}_$size.txt');
      } finally {
        await helper.sl4fDriver.ssh.run('killall iperf3');
      }
    }
  }

  List<void Function()> tests = [];
  void addIperfTest(String label, Protocol proto, Direction direction) {
    tests.add(() {
      test(label, () async {
        final helper = await PerfTestHelper.make();
        await waitSystemMetricsDaemonStart(helper);
        try {
          await runIperfClientTests(helper, proto, label, direction);
        } finally {
          // Kill the iperf3 server process.
          await helper.sl4fDriver.ssh.run('killall iperf3');
        }
      }, timeout: Timeout.none);
    });
  }

  // Localhost tests where both ends of iperf3 sessions are within
  // the target device over loopback interface.
  // Run iperf3 client and capture both sender and receiver stats.
  addIperfTest('localhost_tcp', Protocol.tcp, Direction.loopback);
  addIperfTest('localhost_udp', Protocol.udp, Direction.loopback);

  // Tests across host and target device
  //
  // Run iperf3 to capture data about the send only and receive only
  // performance of the target device, for TCP and UDP. Note that we
  // are only interested in the send/recv performance of the target device.
  addIperfTest('ethernet_tcp_send', Protocol.tcp, Direction.deviceToHost);
  addIperfTest('ethernet_tcp_recv', Protocol.tcp, Direction.hostToDevice);
  addIperfTest('ethernet_udp_send', Protocol.udp, Direction.deviceToHost);
  addIperfTest('ethernet_ucp_recv', Protocol.udp, Direction.hostToDevice);

  runShardTests(args, tests);
}
