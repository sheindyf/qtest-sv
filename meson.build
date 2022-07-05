# All QTests for now are POSIX-only, but the dependencies are
# really in libqtest, not in the testcases themselves.
if not config_host.has_key('CONFIG_POSIX')
  subdir_done()
endif


qtests_generic = [
  'virtio-poc-test',
]
if config_host.has_key('CONFIG_MODULES')
  qtests_generic += [ 'modules-test' ]
endif


qtest_executables = {}
foreach dir : target_dirs
  if not dir.endswith('-softmmu')
    continue
  endif

  target_base = dir.split('-')[0]
  qtest_emulator = emulators['qemu-system-' + target_base]
  target_qtests = qtests_generic

  test_deps = roms
  qtest_env = environment()
  if have_tools
    qtest_env.set('QTEST_QEMU_IMG', './qemu-img')
    test_deps += [qemu_img]
  endif
  qtest_env.set('G_TEST_DBUS_DAEMON', meson.project_source_root() / 'tests/dbus-vmstate-daemon.sh')
  qtest_env.set('QTEST_QEMU_BINARY', './qemu-system-' + target_base)
  if have_tools and have_vhost_user_blk_server
    qtest_env.set('QTEST_QEMU_STORAGE_DAEMON_BINARY', './storage-daemon/qemu-storage-daemon')
    test_deps += [qsd]
  endif

  foreach test : target_qtests
    # Executables are shared across targets, declare them only the first time we
    # encounter them
    if not qtest_executables.has_key(test)
      src = [test + '.c']
      deps = [qemuutil, qos_sv]
      qtest_executables += {
        test: executable(test, src, dependencies: deps)
      }
    endif
    test('qtest-sv-@0@/@1@'.format(target_base, test),
         qtest_executables[test],
         depends: [test_deps, qtest_emulator, emulator_modules],
         env: qtest_env,
         args: ['--tap', '-k'],
         protocol: 'tap',
         timeout: slow_qtests.get(test, 30),
         priority: slow_qtests.get(test, 30),
         suite: ['qtest-sv', 'qtest-sv-' + target_base])
  endforeach
endforeach
