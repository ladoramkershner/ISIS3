// vim: ft=groovy

def isisDataPath = '/isisData/'
def isisTestDataPath = "${isisDataPath}/base/testData"

def isisEnv = [
    "ISIS3DATA=${isisDataPath}",
    "ISIS3TESTDATA=${isisTestDataPath}"
]

def cmakeFlags = [
    "-DJP2KFLAG=OFF",
    "-Dpybindings=OFF",
    "-DCMAKE_BUILD_TYPE=RELEASE"
]

node {
    withEnv(["HOME=/workspace"]) {
        stage("Fedora") {
            docker.image("usgsastro/isis-builder:fedora").inside("-v ${isisDataPath}:${isisDataPath}") {
                stage ("Checkout") {
                    checkout scm
                    isisEnv.add("ISISROOT=${pwd()}/build")
                    cmakeFlags.add("-DCMAKE_INSTALL_PREFIX=${pwd()}/install")
                }

                stage("Create environment") {
                    sh '''
                        # Use the conda cache running on the Jenkins host
                        conda config --set channel_alias http://dmz-jenkins.wr.usgs.gov
                        conda config --set always_yes True
                        conda create -n isis python=3
                        conda env update -n isis -f environment.yml --prune
                    '''
                }
                
                withEnv(isisEnv) {
                    stage ("Build") {
                        dir("build") {
                            sh """
                                source activate isis
                                cmake -GNinja ${cmakeFlags.join(' ')} ../isis
                                ninja -j4 install
                            """
                        }
                    }

                    stage("Test") {
                        sh '''echo Not Implemented'''
                    }
                }
            }
        }
    }
}
