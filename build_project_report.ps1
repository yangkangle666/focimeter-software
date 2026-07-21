$ErrorActionPreference = 'Stop'

function Rgb([int]$r, [int]$g, [int]$b) {
    return [int]($r + ($g -shl 8) + ($b -shl 16))
}

$navy = Rgb 23 50 77
$teal = Rgb 34 166 161
$orange = Rgb 224 122 63
$blue = Rgb 61 126 184
$ink = Rgb 36 48 59
$muted = Rgb 94 111 126
$line = Rgb 210 220 229
$paper = Rgb 247 249 251
$white = Rgb 255 255 255
$softTeal = Rgb 226 245 243
$softOrange = Rgb 252 238 229
$softBlue = Rgb 232 241 250

$root = (Get-Location).Path
$outDir = Join-Path $root 'outputs'
$previewDir = Join-Path $outDir '项目汇报预览'
New-Item -ItemType Directory -Force -Path $outDir, $previewDir | Out-Null
$pptxPath = Join-Path $outDir '焦度计项目汇报.pptx'

$ppt = New-Object -ComObject PowerPoint.Application
$ppt.Visible = 1
$pres = $ppt.Presentations.Add()
$pres.PageSetup.SlideWidth = 13.333 * 72
$pres.PageSetup.SlideHeight = 7.5 * 72

function Add-Text($slide, [string]$text, [double]$left, [double]$top, [double]$width, [double]$height, [double]$size = 20, [int]$color = $ink, [bool]$bold = $false, [int]$align = 1, [string]$font = 'Microsoft YaHei') {
    $text = $text -replace '\\n', [Environment]::NewLine
    $shape = $slide.Shapes.AddTextbox(1, $left, $top, $width, $height)
    $shape.Fill.Visible = 0
    $shape.Line.Visible = 0
    $tf = $shape.TextFrame2
    $tf.MarginLeft = 0
    $tf.MarginRight = 0
    $tf.MarginTop = 0
    $tf.MarginBottom = 0
    $tf.WordWrap = -1
    $tf.AutoSize = 0
    $tf.VerticalAnchor = 1
    $tf.TextRange.Text = $text
    $tf.TextRange.ParagraphFormat.Alignment = $align
    $tf.TextRange.Font.Name = $font
    $tf.TextRange.Font.NameFarEast = $font
    $tf.TextRange.Font.Size = $size
    $tf.TextRange.Font.Bold = $(if($bold){-1}else{0})
    $tf.TextRange.Font.Fill.ForeColor.RGB = $color
    return $shape
}

function Add-Rect($slide, [double]$left, [double]$top, [double]$width, [double]$height, [int]$fill, [int]$border = $fill, [double]$radius = 0) {
    $geometry = $(if($radius -gt 0){5}else{1})
    $shape = $slide.Shapes.AddShape($geometry, $left, $top, $width, $height)
    $shape.Fill.Solid()
    $shape.Fill.ForeColor.RGB = $fill
    $shape.Line.ForeColor.RGB = $border
    $shape.Line.Weight = 1
    return $shape
}

function Add-Circle($slide, [double]$left, [double]$top, [double]$diameter, [int]$fill, [int]$border = $fill) {
    $shape = $slide.Shapes.AddShape(9, $left, $top, $diameter, $diameter)
    $shape.Fill.Solid()
    $shape.Fill.ForeColor.RGB = $fill
    $shape.Line.ForeColor.RGB = $border
    $shape.Line.Weight = 1
    return $shape
}

function Add-Line($slide, [double]$x1, [double]$y1, [double]$x2, [double]$y2, [int]$color = $line, [double]$weight = 1.5, [bool]$arrow = $false) {
    $shape = $slide.Shapes.AddLine($x1, $y1, $x2, $y2)
    $shape.Line.ForeColor.RGB = $color
    $shape.Line.Weight = $weight
    if($arrow){$shape.Line.EndArrowheadStyle = 5}
    return $shape
}

function Add-Chrome($slide, [string]$title, [int]$number) {
    $slide.Background.Fill.Solid()
    $slide.Background.Fill.ForeColor.RGB = $paper
    Add-Rect $slide 0 0 960 8 $teal $teal | Out-Null
    Add-Text $slide $title 54 28 720 38 25 $navy $true | Out-Null
    Add-Text $slide ('焦度计项目  |  ' + ('{0:d2}' -f $number)) 760 32 145 24 10 $muted $false 3 | Out-Null
    Add-Line $slide 54 76 906 76 $line 1 | Out-Null
}

function Add-Label($slide, [string]$text, [double]$left, [double]$top, [double]$width, [int]$fill, [int]$color = $navy) {
    Add-Rect $slide $left $top $width 26 $fill $fill 5 | Out-Null
    Add-Text $slide $text ($left + 8) ($top + 4) ($width - 16) 18 11 $color $true 2 | Out-Null
}

function Add-Module($slide, [string]$name, [string]$role, [double]$left, [double]$top, [double]$width, [int]$fill, [int]$accent) {
    Add-Rect $slide $left $top $width 84 $white $line 8 | Out-Null
    Add-Rect $slide $left $top 7 84 $accent $accent | Out-Null
    Add-Text $slide $name ($left + 20) ($top + 15) ($width - 32) 24 17 $navy $true | Out-Null
    Add-Text $slide $role ($left + 20) ($top + 45) ($width - 32) 26 12 $muted $false | Out-Null
}

# 1. Cover
$slide = $pres.Slides.Add($pres.Slides.Count + 1, 12)
$slide.FollowMasterBackground = 0
$slide.Background.Fill.Solid(); $slide.Background.Fill.ForeColor.RGB = $navy
Add-Rect $slide 0 0 960 12 $teal $teal | Out-Null
Add-Text $slide '自动焦度计项目汇报' 66 120 690 66 34 $white $true | Out-Null
Add-Text $slide '从光学原理、光斑位移到可执行程序' 70 205 650 36 21 (Rgb 191 225 223) $false | Out-Null
Add-Line $slide 70 275 370 275 $orange 4 | Out-Null
Add-Text $slide '汇报目标：建立统一的项目理解，明确当前原型与后续程序化路线' 70 300 570 52 16 $white $false | Out-Null
Add-Text $slide '项目负责人汇报' 70 610 250 24 12 (Rgb 191 205 216) $false | Out-Null

# abstract optical motif
Add-Circle $slide 680 244 44 $orange $orange | Out-Null
Add-Line $slide 724 266 810 266 (Rgb 128 199 196) 2 $true | Out-Null
Add-Rect $slide 810 214 14 104 $softBlue $softBlue | Out-Null
Add-Line $slide 824 242 890 210 (Rgb 128 199 196) 1.5 | Out-Null
Add-Line $slide 824 266 890 266 (Rgb 128 199 196) 1.5 | Out-Null
Add-Line $slide 824 290 890 322 (Rgb 128 199 196) 1.5 | Out-Null
Add-Rect $slide 890 190 12 152 $white $white | Out-Null
Add-Text $slide '光源' 676 296 52 18 11 (Rgb 191 225 223) $false 2 | Out-Null
Add-Text $slide '镜片' 790 336 56 18 11 (Rgb 191 225 223) $false 2 | Out-Null
Add-Text $slide '光斑屏' 858 352 76 18 11 (Rgb 191 225 223) $false 2 | Out-Null

# 2. Why
$slide = $pres.Slides.Add($pres.Slides.Count + 1, 12); Add-Chrome $slide '我们要解决的是“如何可靠地测出镜片屈光力”' 2
Add-Text $slide '镜片的度数不是直接写在图像里，而是藏在光线偏折后的位移中。' 54 105 850 36 20 $navy $true | Out-Null
Add-Rect $slide 54 170 270 320 $softBlue $softBlue 10 | Out-Null
Add-Label $slide '现有问题' 78 198 104 $white $blue
Add-Text $slide '人工调焦\n\n依赖操作经验\n测量效率有限\n难以连续记录\n难以形成可追溯数据' 78 250 205 190 18 $ink $false | Out-Null
Add-Rect $slide 346 170 270 320 $softTeal $softTeal 10 | Out-Null
Add-Label $slide '项目思路' 370 198 104 $white $teal
Add-Text $slide '把镜片折射造成的\n光斑位移拍下来\n\n再用图像处理提取\n光斑坐标并计算屈光力' 370 250 205 190 18 $ink $false | Out-Null
Add-Rect $slide 638 170 270 320 $softOrange $softOrange 10 | Out-Null
Add-Label $slide '本阶段目标' 662 198 112 $white $orange
Add-Text $slide '先建立可解释的\n离线算法原型\n\n再转化为可执行程序\n并用标准和实测数据验证' 662 250 205 190 18 $ink $false | Out-Null
Add-Text $slide '汇报的重点不是“程序已经完成”，而是先把测量原理、数据链路和验证边界讲清楚。' 54 505 852 24 13 $muted $false | Out-Null

# 3. Optical system
$slide = $pres.Slides.Add($pres.Slides.Count + 1, 12); Add-Chrome $slide '测量思路：用光斑位移反推出镜片的光学作用' 3
Add-Text $slide '平行光经过镜片后发生偏折，传播到观察屏时，光斑位置发生改变。' 54 103 852 30 18 $navy $true | Out-Null
Add-Label $slide '光路示意' 54 155 96 $softBlue $blue
Add-Circle $slide 90 278 30 $orange $orange | Out-Null
Add-Text $slide '点光源' 72 324 70 20 13 $muted $false 2 | Out-Null
Add-Line $slide 120 293 212 293 $teal 2 $true | Out-Null
Add-Rect $slide 212 235 16 116 $softBlue $blue | Out-Null
Add-Rect $slide 242 235 16 116 $softBlue $blue | Out-Null
Add-Text $slide '准直镜' 202 370 70 20 13 $muted $false 2 | Out-Null
Add-Line $slide 258 293 368 293 $teal 2 $true | Out-Null
Add-Rect $slide 368 220 54 146 $softOrange $orange 10 | Out-Null
Add-Text $slide '被测\n镜片' 378 263 34 52 14 $navy $true 2 | Out-Null
Add-Line $slide 422 293 520 293 $teal 2 $true | Out-Null
Add-Rect $slide 520 230 14 126 $softTeal $teal | Out-Null
Add-Text $slide '哈特曼\n光阑' 548 263 72 48 14 $navy $true | Out-Null
Add-Line $slide 534 293 660 293 $teal 2 $true | Out-Null
Add-Rect $slide 660 214 16 160 $white $navy | Out-Null
Add-Text $slide '观察屏 / 相机' 626 389 98 20 13 $muted $false 2 | Out-Null
Add-Circle $slide 687 256 12 $teal $teal | Out-Null
Add-Circle $slide 687 286 12 $teal $teal | Out-Null
Add-Circle $slide 687 316 12 $teal $teal | Out-Null
Add-Rect $slide 54 430 852 78 $white $line 8 | Out-Null
Add-Text $slide '关键变量' 78 445 100 22 15 $navy $true | Out-Null
Add-Text $slide '光斑初始位置  →  光斑测量位置  →  位移 Δx、Δy  →  球镜度 / 柱镜度 / 轴位' 204 445 670 22 15 $ink $false | Out-Null
Add-Text $slide '计算必须由实际光路参数和项目 PPT 推导确认，不能直接照搬外部论文公式。' 78 478 780 18 11 $orange $false | Out-Null

# 4. Reference vs measurement
$slide = $pres.Slides.Add($pres.Slides.Count + 1, 12); Add-Chrome $slide '核心数据：比较参考状态与测量状态的五个光斑' 4
Add-Text $slide '标定不是附加步骤，而是把光学系统安装误差从测量结果中分离出来。' 54 103 852 30 18 $navy $true | Out-Null
Add-Label $slide '参考图' 82 160 86 $softBlue $blue
Add-Label $slide '测量图' 456 160 86 $softOrange $orange
Add-Rect $slide 70 210 280 230 $white $line 8 | Out-Null
Add-Rect $slide 444 210 280 230 $white $line 8 | Out-Null
foreach($p in @(@(195,270),@(140,270),@(250,270),@(195,225),@(195,315))){Add-Circle $slide $p[0] $p[1] 18 $blue $blue | Out-Null}
foreach($p in @(@(569,270),@(520,254),@(626,288),@(584,220),@(558,323))){Add-Circle $slide $p[0] $p[1] 18 $orange $orange | Out-Null}
Add-Line $slide 350 290 444 290 $muted 2 $true | Out-Null
Add-Text $slide '同一光阑结构\n不同光斑位置' 347 317 104 38 13 $muted $false 2 | Out-Null
Add-Text $slide '中心点：建立原点\n四周点：确定两个方向\n\n比较两组坐标的差值' 70 452 280 68 14 $ink $false | Out-Null
Add-Text $slide 'Δx = x测量 - x参考\nΔy = y测量 - y参考\n\n位移方向和大小体现镜片的球面、柱面作用' 444 452 330 68 14 $ink $false | Out-Null

# 5. Main control
$slide = $pres.Slides.Add($pres.Slides.Count + 1, 12); Add-Chrome $slide '答辩依据：focimeter.cpp 负责把四个模块串成完整实验流程' 5
Add-Text $slide '它不是最终产品界面，而是用于证明“从图像到度数”可以走通的总控程序。' 54 103 852 30 18 $navy $true | Out-Null
Add-Module $slide 'focimeter.cpp' '总控：组织标定、测量、输出' 370 155 220 $white $navy
Add-Module $slide 'ImProcessor' '图片 → 五个光斑质心' 68 275 230 $softBlue $blue
Add-Module $slide 'CoordSystem' '质心 → 标定坐标系' 365 275 230 $softTeal $teal
Add-Module $slide 'foci' '坐标 → 镜片参数' 662 275 230 $softOrange $orange
Add-Line $slide 480 239 183 275 $muted 1.5 $true | Out-Null
Add-Line $slide 480 239 480 275 $muted 1.5 $true | Out-Null
Add-Line $slide 480 239 777 275 $muted 1.5 $true | Out-Null
Add-Rect $slide 68 400 830 108 $white $line 8 | Out-Null
Add-Text $slide '程序实际执行顺序' 94 424 170 24 16 $navy $true | Out-Null
Add-Text $slide '1 读取标定图\n2 识别五个标记点\n3 建立旋转坐标系\n4 读取测量图' 300 418 230 72 13 $ink $false | Out-Null
Add-Text $slide '5 转换光斑坐标\n6 判断球镜 / 柱镜\n7 输出 Fs、Fc、θ' 570 418 230 72 13 $ink $false | Out-Null
Add-Text $slide '源码位置：focimeter.cpp 的 main 函数；当前输入路径和部分物理参数仍是硬编码。' 68 510 830 20 12 $orange $false | Out-Null

# 6. Image pipeline
$slide = $pres.Slides.Add($pres.Slides.Count + 1, 12); Add-Chrome $slide '图像算法的任务：把复杂背景中的亮斑变成可靠坐标' 6
Add-Text $slide '每一步都服务于同一个目标：降低质心误差，让后面的光学计算有可信输入。' 54 103 852 30 18 $navy $true | Out-Null
$steps = @(
    @('01','ROI','只保留中心区域'),
    @('02','灰度','统一图像表达'),
    @('03','中值滤波','抑制噪声'),
    @('04','顶帽运算','增强小亮斑'),
    @('05','自适应 Otsu','应对光照不均'),
    @('06','连通域','筛选五个区域'),
    @('07','质心','得到光斑坐标')
)
$x = 54
foreach($s in $steps){
    Add-Rect $slide $x 205 112 178 $white $line 8 | Out-Null
    Add-Text $slide $s[0] ($x + 14) 224 82 22 13 $teal $true | Out-Null
    Add-Text $slide $s[1] ($x + 14) 257 84 28 16 $navy $true | Out-Null
    Add-Text $slide $s[2] ($x + 14) 310 84 48 13 $muted $false | Out-Null
    if($x -lt 780){Add-Line $slide ($x + 112) 294 ($x + 130) 294 $teal 1.5 $true | Out-Null}
    $x += 126
}
Add-Rect $slide 54 420 852 88 $softBlue $softBlue 8 | Out-Null
Add-Text $slide '为什么不直接找最亮像素？' 78 443 250 24 15 $navy $true | Out-Null
Add-Text $slide '真实图像存在背景不均、噪声、光斑大小变化和局部粘连。程序先做增强与分割，再通过区域质心获得更稳定的位置。' 338 437 530 44 14 $ink $false | Out-Null

# 7. Coordinate system
$slide = $pres.Slides.Add($pres.Slides.Count + 1, 12); Add-Chrome $slide '坐标标定：让测量结果不依赖相机和光路的旋转方向' 7
Add-Text $slide '五个光斑的几何关系提供了一个局部坐标系。' 54 103 852 30 18 $navy $true | Out-Null
Add-Rect $slide 70 178 340 330 $white $line 8 | Out-Null
Add-Line $slide 240 338 350 338 $blue 2 $true | Out-Null
Add-Line $slide 240 338 240 220 $teal 2 $true | Out-Null
Add-Circle $slide 231 329 18 $navy $navy | Out-Null
Add-Circle $slide 231 239 18 $teal $teal | Out-Null
Add-Circle $slide 145 329 18 $blue $blue | Out-Null
Add-Circle $slide 315 329 18 $blue $blue | Out-Null
Add-Circle $slide 240 415 18 $blue $blue | Out-Null
Add-Text $slide '0 原点' 260 350 72 20 13 $navy $true | Out-Null
Add-Text $slide '1 Y+' 252 238 64 20 13 $teal $true | Out-Null
Add-Text $slide '4 X+' 320 311 64 20 13 $blue $true | Out-Null
Add-Text $slide '图像坐标 → 标定坐标' 144 470 180 20 14 $muted $false 2 | Out-Null
Add-Rect $slide 466 178 440 330 $softTeal $softTeal 8 | Out-Null
Add-Text $slide '标定解决三个问题' 494 208 270 26 18 $navy $true | Out-Null
Add-Text $slide '1  确定原点：避免直接使用图像中心\n\n2  确定方向：消除相机旋转和安装角度\n\n3  统一坐标：让位移分量对应光学方向' 494 260 340 150 17 $ink $false | Out-Null
Add-Text $slide '当前实现用点 0、1、4 建轴，其他点用于保持五点结构。后续需要加入点序稳定性和异常检测。' 494 435 350 42 13 $orange $false | Out-Null

# 8. Current state
$slide = $pres.Slides.Add($pres.Slides.Count + 1, 12); Add-Chrome $slide '当前原型已经打通主链路，但距离可执行软件还有工程化工作' 8
Add-Text $slide '答辩时应把“已完成”和“待验证”分开讲，避免把原型结果表述成最终性能。' 54 103 852 30 18 $navy $true | Out-Null
Add-Label $slide '已有基础' 82 170 100 $softTeal $teal
Add-Label $slide '需要补齐' 508 170 100 $softOrange $orange
Add-Text $slide '✓ OpenCV 图像处理流程\n✓ 五光斑检测与质心计算\n✓ 局部坐标系建立\n✓ 球镜 / 柱镜计算接口\n✓ 已生成 Debug 可执行文件' 82 220 340 190 17 $ink $false | Out-Null
Add-Text $slide '□ 输入路径改为配置或命令行参数\n□ 保存和加载标定结果\n□ 固定单位、距离和像元参数\n□ 增加样本、真值和自动测试\n□ 按 JJG 580 建立误差验证\n□ 再接入相机或图形界面' 508 220 350 220 17 $ink $false | Out-Null
Add-Rect $slide 82 425 776 65 $softBlue $softBlue 8 | Out-Null
Add-Text $slide '当前最重要的下一步：先把公式、参数、输入输出和验收指标写成明确的程序规格。' 110 447 720 24 15 $navy $true 2 | Out-Null

# 9. Roadmap
$slide = $pres.Slides.Add($pres.Slides.Count + 1, 12); Add-Chrome $slide '后续路线：先可解释，再可复现，最后做成可执行程序' 9
Add-Text $slide '项目按照“原理冻结 → 算法验证 → 工程落地”的顺序推进。' 54 103 852 30 18 $navy $true | Out-Null
$road = @(
    @('01','原理冻结','从项目 PPT 和实际光路确认变量、单位、公式和适用条件。',$blue,$softBlue),
    @('02','算法验证','用真实图片输出中间结果、光斑坐标、位移和计算结果。',$teal,$softTeal),
    @('03','程序落地','配置化输入、标定文件、错误提示、测试和标准化输出。',$orange,$softOrange)
)
$x = 74
foreach($r in $road){
    Add-Rect $slide $x 208 240 230 $r[4] $r[4] 10 | Out-Null
    Add-Circle $slide ($x + 20) 232 44 $r[3] $r[3] | Out-Null
    Add-Text $slide $r[0] ($x + 20) 244 44 18 13 $white $true 2 | Out-Null
    Add-Text $slide $r[1] ($x + 82) 238 130 28 18 $navy $true | Out-Null
    Add-Text $slide $r[2] ($x + 22) 305 195 72 15 $ink $false | Out-Null
    if($x -lt 560){Add-Line $slide ($x + 240) 323 ($x + 286) 323 $muted 2 $true | Out-Null}
    $x += 286
}
Add-Text $slide '每一步都要有可检查的产物：公式表、样本输出、误差统计、可运行命令。' 74 495 800 26 16 $muted $false 2 | Out-Null

# 10. Close
$slide = $pres.Slides.Add($pres.Slides.Count + 1, 12); Add-Chrome $slide '本次汇报希望形成三点共识' 10
Add-Rect $slide 70 150 820 250 $navy $navy 10 | Out-Null
Add-Text $slide '1' 106 188 54 58 42 $teal $true 2 | Out-Null
Add-Text $slide '项目测量的是光斑位移，不是直接识别镜片标签。' 182 193 630 36 22 $white $true | Out-Null
Add-Text $slide '2' 106 264 54 58 42 $orange $true 2 | Out-Null
Add-Text $slide 'focimeter.cpp 证明主流程可以串起来，但仍属于答辩原型。' 182 269 630 36 22 $white $true | Out-Null
Add-Text $slide '3' 106 340 54 58 42 $teal $true 2 | Out-Null
Add-Text $slide '下一步先固化原理和验证指标，再把它转换成可执行程序。' 182 345 630 36 22 $white $true | Out-Null
Add-Text $slide '资料依据：项目演示文稿、JJG 580-2005、GB 10810.1-2005。陈文婷论文仅作方法背景与限制参考。' 70 485 820 38 13 $muted $false 2 | Out-Null
Add-Text $slide '谢谢' 70 570 820 40 24 $navy $true 2 | Out-Null

$pres.SaveAs($pptxPath)
for($i = 1; $i -le $pres.Slides.Count; $i++) {
    $png = Join-Path $previewDir ('slide-{0:d2}.png' -f $i)
    $pres.Slides.Item($i).Export($png, 'PNG', 1600, 900)
}
$pres.Close()
$ppt.Quit()
[System.Runtime.InteropServices.Marshal]::ReleaseComObject($pres) | Out-Null
[System.Runtime.InteropServices.Marshal]::ReleaseComObject($ppt) | Out-Null
Write-Output $pptxPath
