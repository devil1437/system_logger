cmake_minimum_required(VERSION 2.8)
PROJECT(PROFILE_SYS)

ADD_CUSTOM_TARGET(push adb push bin/profile_sys /data/local/bin/)
ADD_CUSTOM_TARGET(exec adb shell /data/local/bin/profile_sys)

SET(MY_C_FLAGS "-Wall")
SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${MY_C_FLAGS}")
SET(PROFILE_SYS_SRC profile_sys.c profile_frameRate.c)
ADD_EXECUTABLE(profile_sys ${PROFILE_SYS_SRC})

