
node("windows-1") {
  stage("Build MULINK for Windows") {
    git url: ${GIT_URL}
    dir("build"){
      if(fileExists("Release")) {
        bat 'rmdir "Release" /S /Q'
      }
      bat 'cmake .. -G "Visual Studio 17 2022" -A x64'
      bat 'cmake --build . --config Release -- /nologo /verbosity:minimal /maxcpucount'
      archiveArtifacts artifacts: 'Release/mulink.exe', fingerprint: true
      //deleteDir()
    }
    
    withCredentials([string(credentialsId: 'muon-discord-webhook', variable: 'DISCORD_URL')]) {
     discordSend description: "Build complete", footer: "windows", link: "$BUILD_URL", result: currentBuild.currentResult, title: JOB_NAME, webhookURL: DISCORD_URL
    }
  }
}
node("master") {
  stage("Build MULINK for Linux") {
    environment {
      CC  = '/usr/lib/ccache/gcc'
      CXX = '/usr/lib/ccache/g++'
    }
    git url: ${GIT_URL}
    dir("build"){
      sh 'rm -f mulink'
      sh 'cmake .. -DBUILD_STATIC=true -DUSE_CCACHE=true -DMUST_USE_CCACHE=true'
      sh 'make -j4'
      archiveArtifacts artifacts: 'mulink', fingerprint: true
    }
    
    withCredentials([string(credentialsId: 'muon-discord-webhook', variable: 'DISCORD_URL')]) {
     discordSend description: "Build complete", footer: "linux", link: "$BUILD_URL", result: currentBuild.currentResult, title: JOB_NAME, webhookURL: DISCORD_URL
    }
  }
}
