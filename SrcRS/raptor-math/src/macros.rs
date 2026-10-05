macro_rules! vector_binop {
	($tr:ident, $meth:ident, $t:ty, $rhs:ty, |$a:ident, $b:ident| $body:expr) => {
		impl $tr<$rhs> for $t
		{
			type Output = $t;

			#[inline]
			fn $meth(self, $b: $rhs) -> $t
			{
				let $a = self;
				$body
			}
		}

		impl $tr<&$rhs> for $t
		{
			type Output = $t;

			#[inline]
			fn $meth(self, rhs: &$rhs) -> $t
			{
				$tr::$meth(self, *rhs)
			}
		}

		impl $tr<$rhs> for &$t
		{
			type Output = $t;

			#[inline]
			fn $meth(self, rhs: $rhs) -> $t
			{
				$tr::$meth(*self, rhs)
			}
		}

		impl $tr<&$rhs> for &$t
		{
			type Output = $t;

			#[inline]
			fn $meth(self, rhs: &$rhs) -> $t
			{
				$tr::$meth(*self, *rhs)
			}
		}
	};
}

macro_rules! vector_assign_op {
	($tr:ident, $meth:ident, $op:tt, $t:ty, $rhs:ty) => {
		impl $tr<$rhs> for $t
		{
			#[inline]
			fn $meth(&mut self, rhs: $rhs)
			{
				*self = *self $op rhs;
			}
		}

		impl $tr<&$rhs> for $t
		{
			#[inline]
			fn $meth(&mut self, rhs: &$rhs)
			{
				*self = *self $op *rhs;
			}
		}
	};
}
