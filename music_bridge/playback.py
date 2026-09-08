"""Bounded presentation smoothing for the player's gap between tracks."""

class SkipTransition:
    def __init__(self):
        self.clear()

    def clear(self):
        self.source = None
        self.deadline = 0
        self.paused_at = None

    def arm(self, source, now):
        self.source = source
        self.deadline = now + 3
        self.paused_at = None

    def observe(self, source, available, playing, now):
        if source != self.source or not available or now >= self.deadline:
            self.clear()
        elif playing:
            self.paused_at = None
        elif self.paused_at is None:
            self.paused_at = now

    def holds_playing(self, now):
        # A sustained pause is real even if it immediately follows a skip.
        return (self.source is not None and now < self.deadline and
                self.paused_at is not None and now - self.paused_at < 1)
