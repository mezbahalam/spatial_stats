# frozen_string_literal: true

module SpatialStats
  module Local
    ##
    # GetisOrd's G and G* statistics compute the spatial autocorrelation of a
    # variable, x. G computes the ratio of spatially lagged x to the sum of all
    # other x's except xi for every entry. G* does the same calculation but
    # includes xi in the spatial lag and denominator.
    class GetisOrd < Stat
      ##
      # A new instance of GetisOrd
      #
      # @param [ActiveRecord::Relation] scope
      # @param [Symbol, String] field to query from scope
      # @param [WeightsMatrix] weights to define relationship between observations in scope
      #
      # @return [GetisOrd]
      def initialize(scope, field, weights, star = nil)
        super(scope, field, weights)
        @star = star
      end
      attr_accessor :star

      ##
      # Computes the G or G* statistic for every observation in x.
      #
      # @return [Array] of autocorrelations for each observation.
      def stat
        x.each_with_index.map do |_x_val, idx|
          stat_i(idx)
        end
      end
      alias g stat

      ##
      # Values of the +field+ queried from the +scope+
      #
      # @return [Array]
      def x
        @x ||= SpatialStats::Queries::Variables.query_field(@scope, @field)
      end
      alias z x

      ##
      # True if G* is being used, false if G is being used.
      # If no value is passed in the constructor, it will be determined
      # based off of the trace of the weights.
      #
      # @return [Boolean] of star
      def star?
        if @star.nil?
          @star = weights.full.trace.positive?
        else
          @star
        end
      end

      private

      def stat_i(idx)
        x_lag[idx] / denominators[idx]
      end

      def mc_i(wi, perms, idx)
        x_lag_i = (wi * perms).sum(1)
        x_lag_i / denominators[idx]
      end

      def w
        @w ||= begin
          if star?
            weights.full.windowed.row_standardized
          else
            weights.standardized
          end
        end
      end

      def z_lag
        # window if star is true
        @z_lag ||= begin
          if star?
            SpatialStats::Utils::Lag.window_sum(w, x)
          else
            SpatialStats::Utils::Lag.neighbor_sum(w, x)
          end
        end
      end
      alias x_lag z_lag

      def denominators
        @denominators ||= begin
          n = w.shape[0]
          if star?
            [x.sum] * n
          else
            # add everything but i
            (0..n - 1).each.map do |idx|
              terms = x.dup
              terms.delete_at(idx)
              terms.sum
            end
          end
        end
      end
    end
  end
end