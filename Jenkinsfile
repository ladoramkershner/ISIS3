// vim: ft=groovy

node {
    parallel(
        'centos': {
            stage('CentOS') {
                build 'ISIS Builds/CentOS'
            }
        },
        'debian': {
            stage('Debian') {
                build 'ISIS Builds/Debian'
            }
        },
        'fedora': {
            stage('Fedora') {
                build 'ISIS Builds/Fedora'
            }
        },
        'ubuntu': {
            stage('Ubuntu') {
                build 'ISIS Builds/Ubuntu'
            }
        }
    )
}

