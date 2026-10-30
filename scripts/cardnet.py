#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""cardnet.py —— 麻将/扑克分类器网络定义（只使用 ESP-DL 支持的算子）。"""
import torch.nn as nn


def conv_bn(cin, cout, k=3, s=1):
    return nn.Sequential(
        nn.Conv2d(cin, cout, k, s, k // 2, bias=False),
        nn.BatchNorm2d(cout),
        nn.ReLU(inplace=True),
    )


class DSBlock(nn.Module):
    """Depthwise 3x3 + pointwise 1x1。"""

    def __init__(self, cin, cout, stride=1):
        super().__init__()
        self.dw = nn.Sequential(
            nn.Conv2d(cin, cin, 3, stride, 1, groups=cin, bias=False),
            nn.BatchNorm2d(cin),
            nn.ReLU(inplace=True),
        )
        self.pw = nn.Sequential(
            nn.Conv2d(cin, cout, 1, 1, 0, bias=False),
            nn.BatchNorm2d(cout),
            nn.ReLU(inplace=True),
        )

    def forward(self, x):
        return self.pw(self.dw(x))


class CardNet(nn.Module):
    def __init__(self, n_classes, width=1.0, dropout=0.2):
        super().__init__()
        c = lambda x: max(8, int(round(x * width / 8)) * 8)
        self.stem = conv_bn(3, c(32), 3, 2)
        self.features = nn.Sequential(
            DSBlock(c(32), c(64), 2), DSBlock(c(64), c(64), 1),
            DSBlock(c(64), c(96), 2), DSBlock(c(96), c(96), 1),
            DSBlock(c(96), c(128), 2), DSBlock(c(128), c(128), 1),
            DSBlock(c(128), c(192), 2), DSBlock(c(192), c(192), 1),
            DSBlock(c(192), c(256), 1),
            nn.Conv2d(c(256), c(512), 1, 1, 0, bias=False),
            nn.BatchNorm2d(c(512)),
            nn.ReLU(inplace=True),
        )
        self.pool = nn.AdaptiveAvgPool2d(1)
        self.drop = nn.Dropout(dropout)
        self.fc = nn.Linear(c(512), n_classes)

    def forward(self, x):
        x = self.stem(x)
        x = self.features(x)
        x = self.pool(x).flatten(1)
        return self.fc(self.drop(x))
