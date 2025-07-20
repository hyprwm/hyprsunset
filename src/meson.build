globber = run_command('sh', '-c', 'find . -name "*.cpp" | sort', check : true) src = globber.stdout().strip().split('\n')

                                                                                         executable('hyprsunset', src,
                                                                                                    dependencies :
                                                                                                    [
                                                                                                        dependency('wayland-client'),
                                                                                                        dependency('wayland-cursor'),
                                                                                                        dependency('hyprlang'),
                                                                                                        dependency('hyprutils', version : '>= 0.2.3'),
                                                                                                        dependency('threads'),
                                                                                                    ],
                                                                                                    install : true, )
