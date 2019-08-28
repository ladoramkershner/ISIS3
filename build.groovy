// vim: ft=groovy

def isisDataPath = '/isisData'
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
    cleanWs()
    withEnv(["HOME=${workspace}"]) {
        docker.image("usgsastro/isis-builder:${env.OS.toLowerCase()}").inside("-v ${isisDataPath}:${isisDataPath}") {
            stage ("Checkout") {
                env.STAGE_STATUS = "Checking out ISIS"
                checkout scm
                isisEnv.add("ISISROOT=${pwd()}/build")
                cmakeFlags.add("-DCMAKE_INSTALL_PREFIX=${pwd()}/install")
            }

            stage("Create environment") {
                env.STAGE_STATUS = "Creating conda environment"
                sh '''
                    # Use the conda cache running on the Jenkins host
                    conda config --set channel_alias http://dmz-jenkins.wr.usgs.gov
                    conda config --set always_yes True
                    conda create -n isis python=3
                '''
                if (env.OS.toLowerCase() == "centos") {
                    sh 'conda env update -n isis -f environment.yml --prune'
                } else {
                    sh 'conda env update -n isis -f environment_gcc4.yml --prune'
                }
            }
            
            withEnv(isisEnv) {
                stage ("Build") {
                    env.STAGE_STATUS = "Building ISIS on ${env.OS}"
                    dir("build") {
                        sh """
                            # Webhook test comment
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
