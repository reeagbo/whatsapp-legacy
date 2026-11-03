package org.reeagbo.whatsapplegacy

/** A late local event cannot undo a remote delivery/read confirmation. */
internal object MessageState {
    fun advance(previous: Int, next: Int): Int = when {
        next in 2..4 -> maxOf(previous, next)
        previous >= 2 -> previous
        next < 0 -> next
        previous < 0 -> previous
        next in 0..1 -> maxOf(previous, next)
        else -> previous
    }
}
