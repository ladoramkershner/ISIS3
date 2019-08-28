// vim: ft=groovy

void setBuildStatus(String message, String state) {
  step([
      $class: "GitHubCommitStatusSetter",
      reposSource: [$class: "ManuallyEnteredRepositorySource", url: "https://github.com/"],
      contextSource: [$class: "ManuallyEnteredCommitContextSource", context: "ci/jenkins/build-status"],
      errorHandlers: [[$class: "ChangingBuildStatusErrorHandler", result: "UNSTABLE"]],
      statusResultSource: [ $class: "ConditionalStatusResultSource", results: [[$class: "AnyBuildResult", message: message, state: state]] ]
  ]);
}

node {
    parallel {
        stage('CentOS') {
            build 'ISIS Builds/CentOS'
        }

        stage('Debian') {
            build 'ISIS Builds/Debian'
        }
        
        stage('Fedora') {
            build 'ISIS Builds/Fedora'
        }

        stage('Ubuntu') {
            build 'ISIS Builds/Ubuntu'
        }
    }
}

