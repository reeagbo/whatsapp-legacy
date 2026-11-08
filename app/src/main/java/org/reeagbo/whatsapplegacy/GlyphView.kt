package org.reeagbo.whatsapplegacy

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.view.View
import android.view.accessibility.AccessibilityNodeInfo

/** Small geometric symbols: no font, bitmap, vector-resource or UI-library dependency. */
class GlyphView(context: Context, private val glyph: String, private val tint: Int,
                private val backgroundTint: Int, private val corner: Float = 12f) : View(context) {
    private val density=resources.displayMetrics.density
    private val ink=Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style=Paint.Style.STROKE;strokeWidth=2f;strokeCap=Paint.Cap.ROUND;strokeJoin=Paint.Join.ROUND
    }
    private val fill=Paint(Paint.ANTI_ALIAS_FLAG)
    override fun drawableStateChanged() { super.drawableStateChanged();invalidate() }
    override fun onInitializeAccessibilityNodeInfo(info: AccessibilityNodeInfo) {
        super.onInitializeAccessibilityNodeInfo(info)
        info.className=if(isClickable) "android.widget.Button" else "android.widget.ImageView"
    }
    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        fill.color=backgroundTint
        val bounds=RectF(0f,0f,width.toFloat(),height.toFloat())
        canvas.drawRoundRect(bounds,corner*density,corner*density,fill)
        if(isPressed || isFocused) {
            fill.color=Color.argb(45,Color.red(tint),Color.green(tint),Color.blue(tint))
            canvas.drawRoundRect(bounds,corner*density,corner*density,fill)
        }
        canvas.save()
        val scale=minOf(width,height)/48f
        canvas.translate((width-48f*scale)/2f,(height-48f*scale)/2f)
        canvas.scale(scale,scale)
        ink.color=tint;ink.alpha=if(isEnabled)255 else 85
        fill.color=tint;fill.alpha=if(isEnabled)255 else 85
        fun line(x1: Float,y1: Float,x2: Float,y2: Float)=canvas.drawLine(x1,y1,x2,y2,ink)
        fun path(vararg xy: Float) {
            val p=Path();p.moveTo(xy[0],xy[1])
            var i=2
            while(i<xy.size){p.lineTo(xy[i],xy[i+1]);i+=2}
            canvas.drawPath(p,ink)
        }
        when(glyph) {
            "back" -> path(28f,14f,18f,24f,28f,34f)
            "send" -> {
                ink.strokeWidth = 3f
                line(9f,24f,38f,24f)
                path(25f,11f,38f,24f,25f,37f)
            }
            "plus" -> { line(14f,24f,34f,24f);line(24f,14f,24f,34f) }
            "group" -> {
                canvas.drawCircle(18f,18f,5f,fill)
                canvas.drawCircle(30f,18f,5f,fill)
                canvas.drawCircle(24f,30f,5f,fill)
            }
            "menu" -> for(y in listOf(15f,24f,33f))canvas.drawCircle(24f,y,1.8f,fill)
            "cancel" -> { line(16f,16f,32f,32f);line(32f,16f,16f,32f) }
            "select", "deselect" -> {
                canvas.drawRoundRect(RectF(14f,14f,34f,34f),3f,3f,ink)
                if(glyph=="select")path(18f,24f,22f,28f,30f,19f)
            }
            "pin", "unpin" -> {
                path(18f,13f,30f,13f,28f,16f,28f,23f,33f,28f,15f,28f,20f,23f,20f,16f,18f,13f)
                line(24f,28f,24f,36f)
                if(glyph=="unpin")line(13f,35f,35f,13f)
            }
            "archive", "unarchive" -> {
                canvas.drawRoundRect(RectF(14f,19f,34f,34f),2f,2f,ink)
                canvas.drawRoundRect(RectF(12f,13f,36f,19f),2f,2f,ink)
                if(glyph=="archive")line(21f,25f,27f,25f)
                else { line(24f,30f,24f,23f);path(20f,27f,24f,23f,28f,27f) }
            }
            "delete" -> {
                path(17f,19f,18f,35f,30f,35f,31f,19f)
                line(14f,15f,34f,15f);line(20f,11f,28f,11f)
                line(22f,21f,22f,30f);line(26f,21f,26f,30f)
            }
        }
        canvas.restore()
    }
}
