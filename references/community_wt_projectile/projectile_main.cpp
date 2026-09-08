#include <Windows.h>
#include <iostream>
#include <math.h>
#include "GameFuncs.h"
#include <chrono>

//2D simulation of how War Thunder simulated bullets with drag
struct ShellInfo
{
    vec2 targetCood;
    float BulletMass;     //kg
    float BulletCaliber; //m
    float BulletLength; //m
};

int flySimulation(float angle_degrees) {
    GameFuncs game;
    // Example of War thunders bullet coefficient

    float BulletLength = 0.349999994f; //m
    float BulletMass = 6.019999981f;     //kg
    float BulletCaliber = 0.07500000298f; //m
    float DragConstant = game.GetDragConstant(219.54f);
    float bulletCoeff = game.GetBallisticCoeff(BulletLength, BulletMass, BulletCaliber, DragConstant);;
    float gravity = -9.81f;
    // Degrees. Needed to create a velocity.
    float launchAngle = angle_degrees;
    float bulletSpeed = 304.f; // m/s

    vec2 bulletPosition = { 0,0 };
    vec2 targetPosition = { 1188.99,15.65 };
    vec2 bulletFireDirection = { cos(DEG2RAD(launchAngle)), sin(DEG2RAD(launchAngle)) };
    vec2 bulletVelocity = bulletFireDirection * bulletSpeed;
    //printf("bc: %f\n", bulletCoeff);
    //printf("drag: %f\n", DragConstant);
    for (float t = 0; t < 7.f; t += game.timeStep) {
        if (abs(bulletPosition.x - targetPosition.x) <= 1.f && abs(bulletPosition.y - targetPosition.y) <= 1.f) {

            printf("Time: %f\nPosition [X:%2.2f || Y:%2.2f]\n\n", t, bulletPosition.x, bulletPosition.y);
            return 1;

        }

        else if (abs(bulletPosition.x - targetPosition.x) <= 1.f && bulletPosition.y > targetPosition.y) {
            //printf("Time: %f\nPosition [X:%2.2f || Y:%2.2f]\n\n", t, bulletPosition.x, bulletPosition.y);
            return 2;
        }
        else if (abs(bulletPosition.x - targetPosition.x) <= 1.f && bulletPosition.y < targetPosition.y) {
            //printf("Time: %f\nPosition [X:%2.2f || Y:%2.2f]\n\n", t, bulletPosition.x, bulletPosition.y);
            return 3;
        }
        else if (bulletPosition.x > 3000.f)//判断高度结束模拟 我改为判断距离
            break;
        //printf("Time: %f\nPosition [X:%2.2f || Y:%2.2f]\n\n", t, bulletPosition.x, bulletPosition.y);
        game.ApplyDrag(bulletCoeff, bulletVelocity, bulletPosition);
    }

    printf("Finished\n");
    return -1; // 如果找不到满足条件的角度，返回-1
}
float findBestAngle(float low, float high) {
    const float precision = 0.01; // 你可以根据需要调整这个精度
    while (high - low > precision) {
        float mid = low + (high - low) / 2;
        int result = flySimulation(mid);
        if (result == 1) {
            return mid;
        }
        else if (result == 2) {
            high = mid;
        }
        else if (result == 3) {
            low = mid;
        }
        else {
            return -1;
        }
    }
    return -1; // 如果找不到满足条件的角度，返回-1
}

int main()
{
    // 获取程序开始运行时的时间点
    auto start = std::chrono::steady_clock::now();
    //int aa = hitTarget(6.38f);
    //printf("sadasd: %d\n", aa);
    float aa = findBestAngle(0.f, 40.f);
    printf("best angle: %f\n", aa);
    // 获取程序结束运行时的时间点
    auto end = std::chrono::steady_clock::now();

    // 计算程序运行时间的差值
    auto diff = end - start;

    // 将时间差值转换为毫秒，输出运行时间
    std::cout << "程序运行时间为：" << std::chrono::duration<double, std::milli>(diff).count() << " 毫秒\n";


}
